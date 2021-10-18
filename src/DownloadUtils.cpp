#include <BCY/Base64.h>
#include <BCY/DownloadFilter.hpp>
#include <BCY/DownloadUtils.hpp>
#include <BCY/Utils.hpp>
#include <boost/lexical_cast.hpp>
#include <boost/lockfree/stack.hpp>
#include <boost/log/trivial.hpp>
#include <boost/thread.hpp>
#include <cpprest/http_client.h>
//#include <execinfo.h>
#include <fstream>
#include <regex>
#ifdef __APPLE__
#define BOOST_STACKTRACE_GNU_SOURCE_NOT_REQUIRED
#endif
#include <boost/exception/all.hpp>
#include <boost/stacktrace.hpp>
extern std::wstring_convert<std::codecvt_utf8<wchar_t>> toWstring;
using namespace CryptoPP;
namespace fs = boost::filesystem;
using namespace boost::asio;
using namespace std;
using namespace SQLite;
namespace keywords = boost::log::keywords;
namespace expr = boost::log::expressions;
namespace BCY {
DownloadUtils::DownloadUtils(wstring PathBase, int queryThreadCount,
                             int downloadThreadCount, wstring Path) {

  saveRoot = PathBase;
  fs::path dir(PathBase);
  fs::path file("BCYInfo.db");
  fs::path full_path = dir / file;
  if (Path != L"") {
    DBPath = Path;
  } else {
    DBPath = full_path.wstring();
  }
  Database DB(toWstring.to_bytes(DBPath), SQLite::OPEN_READWRITE | OPEN_CREATE);
  if (queryThreadCount == -1) {
    queryThreadCount = std::thread::hardware_concurrency() / 2;
  }
  if (downloadThreadCount == -1) {
    downloadThreadCount = std::thread::hardware_concurrency() / 2;
  }

  queryThread = new thread_pool(queryThreadCount);
  downloadThread = new thread_pool(downloadThreadCount);
  std::lock_guard<mutex> guard(dbLock);
  DB.exec("CREATE TABLE IF NOT EXISTS UserInfo (uid INTEGER,uname "
          "STRING,self_intro STRING,avatar STRING,isValueUser INTEGER,Tags "
          "wstring NOT NULL DEFAULT '[]',UNIQUE(uid) ON CONFLICT IGNORE)");
  DB.exec("CREATE TABLE IF NOT EXISTS EventInfo (event_id INTEGER,etime "
          "INTEGER,stime INTEGER,cover STRING,intro STRING,Info "
          "STRING,UNIQUE(event_id) ON CONFLICT IGNORE)");
  DB.exec("CREATE TABLE IF NOT EXISTS GroupInfo (gid INTEGER,GroupName "
          "STRING,UNIQUE(gid) ON CONFLICT IGNORE)");
  DB.exec("CREATE TABLE IF NOT EXISTS ItemInfo (uid INTEGER DEFAULT 0,item_id "
          "INTEGER DEFAULT 0,Title wstring NOT NULL DEFAULT '',Tags wstring NOT "
          "NULL DEFAULT '[]',ctime INTEGER DEFAULT 0,Description wstring NOT "
          "NULL DEFAULT '',Images wstring NOT NULL DEFAULT '[]',VideoID wstring "
          "NOT NULL DEFAULT '',UNIQUE (item_id) ON CONFLICT REPLACE)");
  DB.exec("CREATE TABLE IF NOT EXISTS PyBCY (Key wstring DEFAULT '',Value "
          "wstring NOT NULL DEFAULT '',UNIQUE(Key) ON CONFLICT IGNORE)");
  DB.exec("PRAGMA journal_mode=WAL;");
  filter = new DownloadFilter(DBPath);
  // Create Download Temp First
  boost::system::error_code ec;
  fs::path TempPath = fs::path(PathBase) / fs::path("DownloadTemp");
  fs::create_directories(TempPath, ec);
//#warning Remove this after scripting support landed

  filter->addFilterHandler([](DownloadUtils::Info &Inf) {
    std::vector<std::wstring> tags = std::get<3>(Inf);
    if (find(tags.begin(), tags.end(), L"COS") != tags.end()) {
      return 0;
    } else {
      array<wstring, 4> drawKWs = {L"动画", L"漫画", L"绘画", L"手绘"};
      for (wstring ele : drawKWs) {
        if (find(tags.begin(), tags.end(), ele) != tags.end()) {
          return -1;
        }
      }
      return 0;
    }
  });
}
void DownloadUtils::downloadFromAbstractInfo(web::json::value &AbstractInfo) {
  downloadFromAbstractInfo(AbstractInfo, this->enableFilter);
}
void DownloadUtils::downloadFromAbstractInfo(web::json::value &Inf,
                                             bool runFilter) {
  if (stop) {
    return;
  }
#ifndef DEBUG
  boost::asio::post(*queryThread, [=]() {
#endif
    if (stop) {
      return;
    }
    web::json::value AbstractInfo = Inf;
    boost::this_thread::interruption_point();
#ifndef DEBUG
    try {
#endif
      if (runFilter == false ||
          filter->shouldBlockAbstract(
              const_cast<web::json::value &>(AbstractInfo)) == false) {
        if (AbstractInfo.has_field(L"item_detail")) {
          AbstractInfo = AbstractInfo[L"item_detail"];
        }
        wstring item_id = ensure_string(AbstractInfo.at(L"item_id"));

        boost::this_thread::interruption_point();
        optional<DownloadUtils::Info> detail = loadInfo(item_id);
        boost::this_thread::interruption_point();
        if (detail.has_value() == false) {
          boost::this_thread::interruption_point();
          detail.emplace(
              canonicalizeRawServerDetail(core.item_detail(item_id)[L"data"]));
          boost::this_thread::interruption_point();
          try {
            downloadFromInfo(*detail, runFilter);
          } catch (boost::thread_interrupted) {
            BOOST_LOG_TRIVIAL(debug)
                << "Cancelling Thread:" << boost::this_thread::get_id() << endl;
            return;
          }
        } else {
          try {
            downloadFromInfo(*detail, runFilter);
          } catch (boost::thread_interrupted) {
            BOOST_LOG_TRIVIAL(debug)
                << "Cancelling Thread:" << boost::this_thread::get_id() << endl;
            return;
          }
        }
      }
#ifndef DEBUG
    } catch (exception &exp) {
      BOOST_LOG_TRIVIAL(error)
          << exp.what() << __FILE__ << ":" << __LINE__ << endl
          << AbstractInfo.serialize() << endl;
      std::cerr << boost::stacktrace::stacktrace() << '\n';
    }
#endif
#ifndef DEBUG
  });
#endif
}
web::json::value DownloadUtils::saveOrLoadUser(wstring uid, wstring uname,
                                               wstring intro, wstring avatar,
                                               bool isValueUser) {
  std::lock_guard<mutex> guard(dbLock);
  Database DB(toWstring.to_bytes(DBPath), SQLite::OPEN_READWRITE);
  Statement Q(DB, "SELECT uid,uname,self_intro,avatar,isValueUser,Tags FROM "
                  "UserInfo WHERE uid=?");
  Q.bind(1, toWstring.to_bytes(uid));
  Q.executeStep();
  if (Q.hasRow()) {
    web::json::value j;
    j[L"uid"] = web::json::value::string(toWstring.from_bytes(Q.getColumn(0).getString()));
    j[L"uname"] = web::json::value::string(toWstring.from_bytes(Q.getColumn(1).getString()));
    j[L"intro"] = web::json::value::string(toWstring.from_bytes(Q.getColumn(2).getString()));
    j[L"avatar"] = web::json::value::string(toWstring.from_bytes(Q.getColumn(3).getString()));
    int isV = Q.getColumn(4).getInt();
    j[L"isValueUser"] = web::json::value::boolean((isV != 0));
    j[L"Tags"] = web::json::value::parse(toWstring.from_bytes(Q.getColumn(5).getString()));
    return j;
  } else {
    Statement Q(
        DB,
        "INSERT INTO UserInfo(uid,uname,self_intro,avatar,isValueUser,Tags) "
        "VALUES(?,?,?,?,?,?)");
    Q.bind(1, toWstring.to_bytes(uid));
    Q.bind(2, toWstring.to_bytes(uname));
    Q.bind(3, toWstring.to_bytes(intro));
    Q.bind(4, toWstring.to_bytes(avatar));
    Q.bind(5, (isValueUser == 1) ? "1" : "0");
    vector<web::json::value> vals;
    web::json::array tagsArr = core.user_getUserTag(uid)[L"data"].as_array();
    for (web::json::value x : tagsArr) {
      vals.push_back(x[L"ut_name"]);
    }
    Q.bind(6, toWstring.to_bytes(web::json::value::array(vals).serialize()));
    Q.executeStep();
    web::json::value j;
    j[L"uid"] = web::json::value::string(uid);
    j[L"uname"] = web::json::value::string(uname);
    j[L"intro"] = web::json::value::string(intro);
    j[L"avatar"] = web::json::value::string(avatar);
    j[L"isValueUser"] = web::json::value::boolean(isValueUser != 0);
    j[L"Tags"] = web::json::value::array(vals);
    return j;
  }
}
wstring DownloadUtils::loadOrSaveGroupName(wstring name, wstring GID) {
  std::lock_guard<mutex> guard(dbLock);
  Database DB(toWstring.to_bytes(DBPath), SQLite::OPEN_READWRITE);
  Statement Q(DB, "SELECT GroupName FROM GroupInfo WHERE gid=(?)");
  Q.bind(1, toWstring.to_bytes(GID));
  Q.executeStep();
  if (Q.hasRow()) {
    return toWstring.from_bytes(Q.getColumn(0).getString());
  } else {
    Statement insertQuery(
        DB, "INSERT INTO GroupInfo (gid, GroupName) VALUES (?,?)");
    insertQuery.bind(1, toWstring.to_bytes(GID));
    insertQuery.bind(2, toWstring.to_bytes(name));
    insertQuery.executeStep();
    return name;
  }
}
DownloadUtils::Info
DownloadUtils::canonicalizeRawServerDetail(web::json::value Inf) {
  if (!Inf.is_object()) {
    BOOST_LOG_TRIVIAL(error)
        << "Invalid item detal response:" << Inf.serialize() << "\n";
  }
  web::json::value tagsJSON = Inf[L"post_tags"];
  wstring item_id = ensure_string(Inf[L"item_id"]);
  wstring uid = ensure_string(Inf[L"uid"]);
  wstring desc = ensure_string(Inf[L"plain"]);
  vector<wstring> tags; // Inner Param
  for (web::json::value tagD : Inf[L"post_tags"].as_array()) {
    wstring tag = tagD[L"tag_name"].as_string();
    tags.push_back(tag);
  }
  wstring title;
  if (Inf.has_field(L"title") && Inf[L"title"].as_string() != L"") {
    title = Inf[L"title"].as_string();
  } else {
    if (Inf.has_field(L"post_core") && Inf.at(L"post_core").has_field(L"name") &&
        Inf[L"post_core"][L"name"].is_string()) {
      title = Inf[L"post_core"][L"name"].as_string();
    } else {
      if (Inf.has_field(L"ud_id")) {
        wstring val = ensure_string(Inf[L"ud_id"]);
        title = L"日常-" + val;
      } else if (Inf.has_field(L"cp_id")) {
        wstring val = ensure_string(Inf[L"cp_id"]);
        title = L"Cosplay-" + val;
      } else if (Inf.has_field(L"dp_id")) {
        wstring val = ensure_string(Inf[L"dp_id"]);
        title = L"绘画" + val;
      } else if (Inf.has_field(L"post_id")) {
        wstring GID = ensure_string(Inf[L"group"][L"gid"]);
        wstring GroupName =
            loadOrSaveGroupName(Inf[L"group"][L"name"].as_string(), GID);
        wstring val = L"";
        val = ensure_string(Inf[L"ud_id"]);
        title = GroupName + L"-" + val;
      } else if (Inf.has_field(L"item_id")) {
        wstring val = ensure_string(Inf[L"item_id"]);
        if (Inf[L"type"].as_string() == L"works") {
          title = L"Cosplay-" + val;
        } else if (Inf[L"type"].as_string() == L"preview") {
          title = L"预告-" + val;
        } else if (Inf[L"type"].as_string() == L"daily") {
          title = L"日常-" + val;
        } else if (Inf[L"type"].as_string() == L"video") {
          title = L"视频-" + val;
        } else {
          title = val;
        }
      }
    }
  }
  if (title == L"") {
    title = item_id;
  }
  title = loadTitle(title, item_id);
  vector<web::json::value> URLs;
  if (Inf.has_field(L"multi")) {
    for (web::json::value item : Inf[L"multi"].as_array()) {
      URLs.push_back(item);
    }
  }
  if (Inf.has_field(L"cover")) {
    web::json::value j;
    j[L"type"] = web::json::value("image");
    j[L"path"] = Inf[L"cover"];
    URLs.emplace_back(j);
  }
  if (Inf.has_field(L"content")) {
    wregex rgx(L"<img src=\"(.{80,100})\" alt=");
    wstring tmpjson = Inf[L"content"].as_string();
    wsmatch matches;
    while (regex_search(tmpjson, matches, rgx)) {
      web::json::value j;
      wstring URL = matches[1];
      j[L"type"] = web::json::value("image");
      j[L"path"] = web::json::value(URL);
      URLs.emplace_back(j);
      tmpjson = matches.suffix();
    }
  }
  if (Inf.has_field(L"group") && Inf[L"group"].has_field(L"multi")) {
    for (web::json::value foo : Inf[L"group"][L"multi"].as_array()) {
      URLs.push_back(foo);
    }
  }
  wstring vid;
  if (Inf.has_field(L"type") && Inf[L"type"].as_string() == L"video" &&
      Inf.has_field(L"video_info") && downloadVideo == true) {
    vid = Inf[L"video_info"][L"vid"].as_string();
  }

  saveOrLoadUser(uid, ensure_string(Inf[L"profile"][L"uname"]), desc,
                 ensure_string(Inf[L"profile"][L"avatar"]),
                 Inf[L"profile"][L"value_user"].as_bool());
  web::json::array multi = web::json::value::array(URLs).as_array();
  auto tup = std::tuple<std::wstring /*UID*/, std::wstring /*item_id*/,
                        std::wstring /*Title*/, vector<wstring> /*Tags*/,
                        std::wstring /*ctime*/, std::wstring /*Description*/,
                        web::json::array /*multi*/, std::wstring /*videoID*/>(
      uid, item_id, title, tags, ensure_string(Inf[L"ctime"]), desc, multi, vid);
  boost::this_thread::interruption_point();
  BOOST_LOG_TRIVIAL(debug) << "Saving Info For: " << title << endl;
  saveInfo(tup);
  boost::this_thread::interruption_point();
  return tup;
}
wstring DownloadUtils::loadTitle(wstring title, wstring item_id) {
  string query = "SELECT Title FROM ItemInfo WHERE item_id=(?)";
  std::lock_guard<mutex> guard(dbLock);
  Database DB(toWstring.to_bytes(DBPath), OPEN_READONLY);
  Statement Q(DB, query);
  Q.bind(1, toWstring.to_bytes(item_id));
  boost::this_thread::interruption_point();
  Q.executeStep();
  if (Q.hasRow()) {
    wstring T = toWstring.from_bytes(Q.getColumn("Title").getString());
    if (T != L"") {
      return T;
    }
  }
  return title;
}
void DownloadUtils::saveInfo(DownloadUtils::Info Inf) {
  vector<wstring> vals;
  string query = "INSERT INTO ItemInfo "
                 "(uid,item_id,Title,Tags,ctime,Description,Images,VideoID) "
                 "VALUES(?,?,?,?,?,?,?,?)";
  vals.push_back(std::get<0>(Inf));
  vals.push_back(std::get<1>(Inf));
  vals.push_back(std::get<2>(Inf));
  vector<wstring> tags = std::get<3>(Inf);
  vector<web::json::value> tmps;
  for (wstring foo : tags) {
    tmps.push_back(web::json::value(foo));
  }
  vals.push_back(web::json::value::array(tmps).serialize());
  vals.push_back(std::get<4>(Inf));
  vals.push_back(std::get<5>(Inf));
  tmps.clear();
  for (web::json::value x : std::get<6>(Inf)) {
    tmps.push_back(x);
  }
  vals.push_back(web::json::value::array(tmps).serialize());
  vals.push_back(std::get<7>(Inf));
  std::lock_guard<mutex> guard(dbLock);
  Database DB(toWstring.to_bytes(DBPath), SQLite::OPEN_READWRITE);
  Statement Q(DB, query);
  for (decltype(vals.size()) i = 0; i < vals.size(); i++) {
    Q.bind(i + 1, toWstring.to_bytes(vals[i]));
  }
  Q.executeStep();
}
optional<DownloadUtils::Info> DownloadUtils::loadInfo(wstring item_id) {
  std::lock_guard<mutex> guard(dbLock);
  Database DB(toWstring.to_bytes(DBPath), SQLite::OPEN_READONLY);
  Statement Q(
      DB, "SELECT uid,item_id,Title,Tags,ctime,Description,Images,VideoID FROM "
          "ItemInfo WHERE item_id=?");
  Q.bind(1, toWstring.to_bytes(item_id));
  Q.executeStep();
  if (Q.hasRow()) {
    web::json::value v;
    wstring uid = toWstring.from_bytes(Q.getColumn(0).getString());
    wstring item_id = toWstring.from_bytes(Q.getColumn(1).getString());
    wstring title = toWstring.from_bytes(Q.getColumn(2).getString());
    web::json::array tagsArr =
        web::json::value::parse(toWstring.from_bytes(Q.getColumn(3).getString())).as_array();
    vector<wstring> tags;
    for (web::json::value val : tagsArr) {
      tags.push_back(val.as_string());
    }
    wstring ctime = toWstring.from_bytes(Q.getColumn(4).getString());
    wstring desc = toWstring.from_bytes(Q.getColumn(5).getString());
    web::json::array multi =
        web::json::value::parse(toWstring.from_bytes(Q.getColumn(6).getString())).as_array();
    wstring videoID = toWstring.from_bytes(Q.getColumn(7).getString());
    auto tup = std::tuple<std::wstring /*UID*/, std::wstring /*item_id*/,
                          std::wstring /*Title*/, vector<wstring> /*Tags*/,
                          std::wstring /*ctime*/, std::wstring /*Description*/,
                          web::json::array /*multi*/, std::wstring /*videoID*/>(
        uid, item_id, title, tags, ctime, desc, multi, videoID);
    BOOST_LOG_TRIVIAL(debug) << "Loaded Cached Info for:" << item_id << "\n";
    return tup;
  } else {
    BOOST_LOG_TRIVIAL(debug) << "Cached Info for:" << item_id << " not found\n";
    return {};
  }
}
void DownloadUtils::downloadEvent(wstring event_id) {
  //  DB.exec("CREATE TABLE IF NOT EXISTS EventInfo (event_id INTEGER,etime
  //  INTEGER,stime INTEGER,cover STRING,intro STRING,UNIQUE(event_id) ON
  //  CONFLICT IGNORE)");
  web::json::value det = loadEventInfo(event_id);
  if (det.is_null()) {
    det = core.event_detail(event_id)[L"data"];
    insertEventInfo(det);
  }
  core.event_listPosts(event_id, Core::Order::Index, downloadCallback);
}
web::json::value DownloadUtils::loadEventInfo(wstring event_id) {
  std::lock_guard<mutex> guard(dbLock);
  Database DB(toWstring.to_bytes(DBPath), SQLite::OPEN_READWRITE);
  Statement Q(DB, "SELECT Info FROM EventInfo WHERE event_id=?");
  Q.bind(1, toWstring.to_bytes(event_id));
  Q.executeStep();
  if (Q.hasRow()) {
    return web::json::value::parse(toWstring.from_bytes(Q.getColumn(0).getString()));
  } else {
    return web::json::value();
  }
}
void DownloadUtils::insertEventInfo(web::json::value Inf) {
  std::lock_guard<mutex> guard(dbLock);
  Database DB(toWstring.to_bytes(DBPath), SQLite::OPEN_READWRITE);
  Statement insertQuery(
      DB, "INSERT INTO EventInfo (event_id,etime,stime,cover,intro,Info) "
          "VALUES (?,?,?,?,?,?)");
  insertQuery.bind(1, Inf[L"event_id"].as_integer());
  insertQuery.bind(2, Inf[L"etime"].as_integer());
  insertQuery.bind(3, Inf[L"stime"].as_integer());
  insertQuery.bind(4, toWstring.to_bytes(Inf[L"cover"].as_string()));
  insertQuery.bind(5, toWstring.to_bytes(Inf[L"intro"].as_string()));
  insertQuery.bind(6, toWstring.to_bytes(Inf.serialize()));
  insertQuery.executeStep();
}
void DownloadUtils::downloadFromInfo(DownloadUtils::Info Inf, bool runFilter) {
  /*
  Title usually contains UTF8 characters which causes trouble on certain
  platforms. (I'm looking at you Windowshit) Migrate to item_id based ones
  */
  if (runFilter && filter->shouldBlockItem(Inf)) {
    return;
  }
  wstring uid = std::get<0>(Inf);
  wstring item_id = std::get<1>(Inf);
  wstring title = std::get<2>(Inf);
  std::vector<std::wstring> tags = std::get<3>(Inf);
  wstring ctime = std::get<4>(Inf);
  wstring desc = std::get<5>(Inf);
  vector<web::json::value> multi;
  wstring videoID = std::get<7>(Inf);
  for (web::json::value x : std::get<6>(Inf)) {
    multi.push_back(x);
  }
  fs::path savePath = getItemPath(uid, item_id);

  if (multi.size() > 0) {
    boost::system::error_code ec;
    fs::create_directories(savePath, ec);
    if (ec) {
      BOOST_LOG_TRIVIAL(error) << "FileSystem Error: " << ec.message() << "@"
                               << __FILE__ << ":" << __LINE__ << endl;
    }
  } else {
    BOOST_LOG_TRIVIAL(error) << item_id << " has no item to download" << endl;
    return;
  }
  vector<web::json::value>
      a2Methods; // Used for aria2 RPC's ``system.multicall``

  // videoInfo
  if (videoID != L"" && downloadVideo == true) {
    boost::this_thread::interruption_point();
    web::json::value F = core.videoInfo(videoID);
    boost::this_thread::interruption_point();
    web::json::value videoList = F[L"data"][L"video_list"];
    // Find the most HD one
    int bitrate = 0;
    wstring vid = L"video_1";
    if (videoList.is_null()) {
      BOOST_LOG_TRIVIAL(error)
          << "Can't query DownloadURL for videoID:" << vid << endl;
    } else {
      for (auto it = videoList.as_object().cbegin();
           it != videoList.as_object().cend(); ++it) {
        wstring K = it->first;
        web::json::value V = it->second;
        if (V[L"bitrate"].as_integer() > bitrate) {
          vid = K;
        }
      }
      if (videoList.size() > 0) {
        // Videos needs to be manually reviewed before playable
        string URL = "";
        Base64::Decode(toWstring.to_bytes(videoList[vid][L"main_url"].as_string()), &URL);
        wstring FileName = videoID + L".mp4";
        web::json::value j;
        j[L"path"] = web::json::value(toWstring.from_bytes(URL));
        j[L"FileName"] = web::json::value(FileName);

        multi.push_back(j);
      } else {
        BOOST_LOG_TRIVIAL(info)
            << item_id << " hasn't been reviewed yet and thus not downloadable"
            << endl;
      }
    }
  }
  // Good job on fixing the old bug where a user could just obtain
  // un-watermarked image by simply stripping the URL a bit, SIX FUCKING YEARS
  // AGO. Now let's hope someone didn't left un-watermarked image's URL in the
  // new response because that would be very retarded

  // Wait, Ooooops
  vector<web::json::value> placeholders;
  bool shouldInvokeAPI = false;
  for (web::json::value item : multi) {
    wstring URL = item[L"path"].as_string();
    if (URL.find(L"bcy.byteimg.com") == string::npos && URL.length() != 0) {
      // Some old API bug which results in junk URL in response. Good job
      // ByteDance.
      if (URL.find(L"http") == string::npos) {
        if (URL[0] == '/') {
          URL = L"https://img-bcy-qn.pstatp.com" + URL;
        } else if (URL[0] == 'u' && URL[1] == 's') {
          URL = L"https://img-bcy-qn.pstatp.com/" + URL;
        } else {
          BOOST_LOG_TRIVIAL(debug)
              << "Potential Junk URL Detected:" << URL << endl;
          continue;
        }
      }
      web::json::value newEle;
      wstring tmp = URL.substr(URL.find_last_of(L"/"), string::npos);
      wstring origURL = URL;
      if (tmp.find(L".") == string::npos) {
        origURL = URL.substr(0, URL.find_last_of(L"/"));
      }
      wstring FileName = origURL.substr(origURL.find_last_of(L"/") + 1);
      newEle[L"path"] = web::json::value(origURL);
      newEle[L"FileName"] = web::json::value(FileName);
      BOOST_LOG_TRIVIAL(debug)
          << "Extracted URL: " << origURL << " from Stripping URL: " << URL
          << " and FileName: " << FileName << endl;

      placeholders.push_back(newEle);
    } else if (URL.find(L"bcy.byteimg.com") != string::npos) {
      shouldInvokeAPI = true;
    } else {
      BOOST_LOG_TRIVIAL(debug) << "Unhandled URL:" << URL << endl;
    }
  }
  if (shouldInvokeAPI) {
    web::json::value APIRep = core.image_postCover(item_id);

    /*
      Previously BruteForcing original un-watermarked un-compressed Image URL
      was as easy as stripping /w650 from the URL. For new works with URL
      originating from *bcy.byteimg.com,brute-forcing is no longer possible.
      Until someone figures out the algorithm of the sig, which is likely
      generated server side unless some employee *wink* leaks it out
      */
    web::json::array URLs = APIRep[L"data"][L"multi"].as_array();
    if (URLs.size() == 0) {
      BOOST_LOG_TRIVIAL(debug)
          << "item_id:" << item_id << " is possibly locked" << endl;
      /*
        Emulating sig (which is very likely SHA1 with salt,
        not even HMAC judging from their long history of crappy crypto
        implementation They used to just prefix/suffix some magic wstring and
        hash it) In all fairness there are multiple server-side implementation
        exploits to bypass this signature restriction. However none of those
        helps in our use-case since the CDN itself is protected. You can't
        download the image even with proper sig if the item itself is
        locked/deleted. The proper implementation here would be fallback to w650
        images.
        TODO: Cache signatured URLs and see how that works with item_id locking
      */
    }
    wregex byteimgrgx(L"byteimg\\.com\\/img\\/banciyuan\\/([a-zA-Z0-9]*)~");
    for (web::json::value item : URLs) {
      wstring URL = item[L"path"].as_string();
      web::json::value newEle;
      newEle[L"path"] = web::json::value(URL);
      wsmatch matches;
      if (regex_search(URL, matches, byteimgrgx)) {
        assert(matches.size() == 2 &&
               "Regex Finding FileName Met Unexpected Result!");
        wstring fileName = matches[1].str() + L".jpg";
        newEle[L"FileName"] = web::json::value(fileName);

        BOOST_LOG_TRIVIAL(debug) << "Extracted FileName: " << fileName
                                 << " from coverURL: " << URL << endl;
        placeholders.push_back(newEle);
      } else if (URL.find(L"img-bcy-qn.pstatp.com") != string::npos) {
        // Originated from pstatp CDNs which our first step of pre-processing
        // should have already handled. Ignore it
        /* Or use pstatp.com\/user.*?\/([a-zA-Z0-9.]*?)\?imageMogr2 as regex to
         * download again. meh*/
        continue;
      } else {
        BOOST_LOG_TRIVIAL(error)
            << "Regex Extracting ByteImage FileName From URL: " << URL
            << " Failed" << endl;
        return;
      }
    }
  }

  multi = placeholders;

  for (web::json::value item : multi) {
    boost::this_thread::interruption_point();
    wstring URL = item[L"path"].as_string();
    if(URL.find(L"bcyimg")!=string::npos){
      // Old CDN's SSL Certificate is invalid after 2019/12/13
      auto idx = URL.find(L"https://");
      if (idx != string::npos) {
        URL.replace(idx, 8, L"http://");
      }
    }
    wstring FileName = item[L"FileName"].as_string();
    fs::path newFilePath = savePath / fs::path(FileName);
    if (!newFilePath.has_extension()) {
      newFilePath.replace_extension(".jpg");
    }
    boost::system::error_code ec2;
    auto newa2confPath = fs::path(newFilePath.string() + ".aria2");

    bool shouldDL =
        (!fs::exists(newFilePath, ec2) || fs::exists(newa2confPath, ec2));
    BOOST_LOG_TRIVIAL(debug)
        << "Deciding if should download using item path: " << newFilePath
        << " and Aria2 Config Path: " << newa2confPath
        << " Result: " << shouldDL << endl;
    if (shouldDL) {
      if (RPCServer == L"") {
        if (stop) {
          return;
        }
        fs::remove(newa2confPath, ec2);
        fs::remove(newFilePath, ec2);
        boost::asio::post(*downloadThread, [=]() {
          if (stop) {
            return;
          }
          try {
            boost::this_thread::interruption_point();
            auto R = core.GET(URL);
            boost::this_thread::interruption_point();
            vector<unsigned char> vec = R.extract_vector().get();
            if (vec.size() > 0 && vec[0] != '{') {
              ofstream ofs(newFilePath.string(), ios::binary);
              ofs.write(reinterpret_cast<const char *>(vec.data()), vec.size());
              ofs.close();
            }

            boost::this_thread::interruption_point();
          } catch (const std::exception &exp) {
            BOOST_LOG_TRIVIAL(error)
                << "Download from " << URL
                << " failed with exception:" << exp.what() << endl;
          }
        });
      } else {
        boost::this_thread::interruption_point();
        web::json::value rpcparams;
        vector<web::json::value> params; // Inner Param
        vector<web::json::value> URLs;
        URLs.push_back(web::json::value(URL));
        if (secret != L"") {
          params.push_back(web::json::value(L"token:" + secret));
        }
        params.push_back(web::json::value::array(URLs));
        web::json::value options;
        options[L"dir"] = web::json::value(savePath.wstring());
        options[L"out"] = web::json::value(newFilePath.filename().wstring());
        options[L"auto-file-renaming"] = web::json::value("false");
        if (URL.find(L"?sig=") == string::npos) {
          options[L"allow-overwrite"] = web::json::value("false");
        } else {
          options[L"allow-overwrite"] = web::json::value("true");
        }
        options[L"user-agent"] = web::json::value(
            "bcy 4.5.2 rv:4.5.2.6146 (iPad; iPhoneOS 15.3.3; en_US) Cronet");
        wstring gid = md5(URL).substr(0, 16);
        options[L"gid"] = web::json::value(gid);
        params.push_back(options);
        /*
         Video URLs have a (very short!) valid time window
         so we insert those to the start of download queue
        */
        // params.push_back(web::json::value(1));

        rpcparams[L"params"] = web::json::value::array(params);
        rpcparams[L"methodName"] = web::json::value("aria2.addUri");
        a2Methods.push_back(rpcparams);
        boost::this_thread::interruption_point();
      }
    }
  }
  if (a2Methods.size() > 0) {
    try {

      web::json::value arg;
      arg[L"jsonrpc"] = web::json::value("2.0");
      arg[L"id"] = web::json::value();
      arg[L"method"] = web::json::value("system.multicall");
      vector<web::json::value> tmp;
      tmp.push_back(web::json::value::array(a2Methods));
      arg[L"params"] = web::json::value::array(tmp);
      web::http::client::http_client client(RPCServer);
      web::json::value rep =
          client.request(web::http::methods::POST, L"/", arg)
              .get()
              .extract_json(true)
              .get();

      if (rep.has_field(L"result")) {
        BOOST_LOG_TRIVIAL(debug)
            << item_id
            << " Registered in Aria2 with GID:" << rep[L"result"].serialize()
            << " Query:" << arg.serialize() << " item_id:" << item_id << endl;
      } else {
        BOOST_LOG_TRIVIAL(error)
            << item_id << " Failed to Register with Aria2. Response:"
            << rep[L"error"][L"message"].as_string()
            << " Query:" << arg.serialize() << " item_id:" << item_id << endl;
      }
    } catch (const std::exception &exp) {
      BOOST_LOG_TRIVIAL(error) << "Posting to Aria2 Error:" << exp.what()
                               << " item_id:" << item_id << endl;
    }
  } else {
    BOOST_LOG_TRIVIAL(debug)
        << "A2 Params for: " << item_id << " is empty" << endl;
  }
}
void DownloadUtils::verifyUID(wstring UID, bool reverse) {
  verify(L"WHERE uid=?", {UID}, reverse);
}
void DownloadUtils::verifyTag(wstring Tag, bool reverse) {
  verify(L"WHERE Tags LIKE ?", {L"%" + Tag + L"%"}, reverse);
}
void DownloadUtils::unlikeCached() {
  vector<web::json::value> Liked = core.space_getUserLikeTimeLine(core.UID);
  BOOST_LOG_TRIVIAL(info) << "Found " << Liked.size() << " Liked Works" << endl;
  thread_pool t(16);
  for (web::json::value j : Liked) {
    wstring item_id = ensure_string(j[L"item_detail"][L"item_id"]);
    boost::asio::post(t, [=] {
      if (loadInfo(item_id).has_value()) {
        if (core.item_cancelPostLike(item_id)) {
          BOOST_LOG_TRIVIAL(info)
              << "Unlike item_id: " << item_id << " Success" << endl;
        } else {
          BOOST_LOG_TRIVIAL(info)
              << "Unlike item_id: " << item_id << " Failed" << endl;
        }
      } else {
        BOOST_LOG_TRIVIAL(info)
            << "item_id: " << item_id << " Not Found In Local Cache" << endl;
      }
    });
  }
  BOOST_LOG_TRIVIAL(info) << "Joining Unlike Threads\n";
  t.join();
}
void DownloadUtils::verify(std::wstring condition, std::vector<std::wstring> args,
                           bool reverse) {

  BOOST_LOG_TRIVIAL(info) << "Verifying..." << endl;
  vector<wstring> IDs;
  BOOST_LOG_TRIVIAL(info) << "Collecting Cached Infos" << endl;
  {
    std::lock_guard<mutex> guard(dbLock);
    Database DB(toWstring.to_bytes(DBPath), SQLite::OPEN_READONLY);
    Statement Q(DB, "SELECT item_id FROM ItemInfo " + toWstring.to_bytes(condition));
    for (decltype(args.size()) i = 1; i <= args.size(); i++) {
      Q.bind(i, toWstring.to_bytes(args[i - 1]));
    }
    while (Q.executeStep()) {
      IDs.push_back(toWstring.from_bytes(Q.getColumn(0).getString()));
    }
  }
  BOOST_LOG_TRIVIAL(info) << "Found " << IDs.size() << " items" << endl;
  thread_pool t(16);
  if (reverse) {
    vector<wstring>::iterator i = IDs.end();
    while (i != IDs.begin()) {
      --i;
      wstring item_id = *i;
      boost::asio::post(*queryThread, [=] {
        try {
          DownloadUtils::Info inf = *(loadInfo(item_id));
          downloadFromInfo(inf);
        } catch (const std::exception &exp) {
          BOOST_LOG_TRIVIAL(error) << "Verify Failed for item_id: " << item_id
                                   << " message: " << exp.what() << endl;
        }
      });
    }
  } else {
    for (wstring item_id : IDs) {
      boost::asio::post(*queryThread, [=] {
        try {
          DownloadUtils::Info inf = *(loadInfo(item_id));

          downloadFromInfo(inf);
        } catch (const std::exception &exp) {
          BOOST_LOG_TRIVIAL(error) << "Verify Failed for item_id: " << item_id
                                   << " message: " << exp.what() << endl;
        }
      });
    }
  }
}
void DownloadUtils::cleanUID(wstring UID) {
  BOOST_LOG_TRIVIAL(debug) << "Cleaning up UID:" << UID << endl;
  boost::system::error_code ec;
  fs::path UserPath = getUserPath(UID);
  bool isDirec = is_directory(UserPath, ec);
  if (isDirec) {
    fs::remove_all(UserPath, ec);
    BOOST_LOG_TRIVIAL(debug) << "Removed " << UserPath.string() << endl;
  }

  std::lock_guard<mutex> guard(dbLock);
  Database DB(toWstring.to_bytes(DBPath), SQLite::OPEN_READWRITE);
  Statement Q(DB, "DELETE FROM ItemInfo WHERE UID=" + toWstring.to_bytes(UID));
  Q.executeStep();
}
void DownloadUtils::cleanItem(wstring item_id) {
  BOOST_LOG_TRIVIAL(debug) << "Cleaning up Item:" << item_id << endl;
  std::lock_guard<mutex> guard(dbLock);
  Database DB(toWstring.to_bytes(DBPath), SQLite::OPEN_READWRITE);
  Statement Q(DB, "DELETE FROM ItemInfo WHERE item_id=" + toWstring.to_bytes(item_id));
  Q.executeStep();
}
fs::path DownloadUtils::getUserPath(wstring UID) {
  while (UID.length() < 3) {
    UID = L"0" + UID;
  }
  wstring L1Path = wstring(1, UID[0]);
  wstring L2Path = wstring(1, UID[1]);
  fs::path UserPath =
      fs::path(saveRoot) / fs::path(L1Path) / fs::path(L2Path) / fs::path(UID);
  return UserPath;
}
fs::path DownloadUtils::getItemPath(wstring UID, wstring item_id) {
  return getUserPath(UID) / fs::path(item_id);
}
void DownloadUtils::cleanTag(wstring Tag) {
  BOOST_LOG_TRIVIAL(info) << "Cleaning up Tag:" << Tag << endl;
  std::lock_guard<mutex> guard(dbLock);
  Database DB(toWstring.to_bytes(DBPath), SQLite::OPEN_READWRITE);
  Statement Q(DB,
              "SELECT UID,Title,Info,item_id FROM ItemInfo WHERE Tags Like ?");
  Q.bind(1, "%%\"" + toWstring.to_bytes(Tag) + "\"%%");
  while (Q.executeStep()) {
    wstring UID = toWstring.from_bytes(Q.getColumn(0).getString());
    wstring Title = toWstring.from_bytes(Q.getColumn(1).getString());
    wstring Info = toWstring.from_bytes(Q.getColumn(2).getString());
    wstring item_id = toWstring.from_bytes(Q.getColumn(3).getString());
    if (item_id == L"" || item_id == L"0") {
      continue;
    }
    boost::system::error_code ec;
    fs::path UserPath = getUserPath(UID);
    bool isDirec = is_directory(UserPath, ec);
    if (isDirec) {
      fs::remove_all(UserPath, ec);
      if (ec) {
        BOOST_LOG_TRIVIAL(error) << "FileSystem Error: " << ec.message() << "@"
                                 << __FILE__ << ":" << __LINE__ << endl;
      } else {
        BOOST_LOG_TRIVIAL(debug) << "Removed " << UserPath.string() << endl;
      }
    }
  }
}
void DownloadUtils::cleanup() {
  BOOST_LOG_TRIVIAL(info) << "Cleaning up..." << endl;
  thread_pool t(16);
  for (decltype(filter->UIDList.size()) i = 0; i < filter->UIDList.size();
       i++) {
    boost::asio::post(t, [=]() {
      wstring UID = filter->UIDList[i];
      BOOST_LOG_TRIVIAL(debug) << "Removing UID: " << UID << endl;
      cleanUID(UID);
    });
  }
  // vector<string> Infos;
  for (decltype(filter->TagList.size()) i = 0; i < filter->TagList.size();
       i++) {
    boost::asio::post(t, [=]() {
      wstring Tag = filter->TagList[i];
      BOOST_LOG_TRIVIAL(debug) << "Removing Tag: " << Tag << endl;
      cleanTag(Tag);
    });
  }
  t.join();
}
wstring DownloadUtils::md5(wstring &str) {
  string digest;
  Weak::MD5 md5;
  StringSource(toWstring.to_bytes(str), true,
               new HashFilter(md5, new HexEncoder(new StringSink(digest))));
  return toWstring.from_bytes(digest);
}
void DownloadUtils::downloadLiked() {
  if (core.UID != L"") {
    downloadUserLiked(core.UID);
  } else {
    BOOST_LOG_TRIVIAL(error)
        << "Not Logged In. Can't Download Liked Work" << endl;
  }
}
void DownloadUtils::downloadSearchKeyword(wstring KW) {
  BOOST_LOG_TRIVIAL(info) << "Iterating Searched Works For Keyword:" << KW
                          << endl;
  auto l = core.search(KW, Core::SearchType::Content, downloadCallback);

  BOOST_LOG_TRIVIAL(info) << "Found " << l.size()
                          << " Searched Works For Keyword:" << KW << endl;
}
void DownloadUtils::downloadUser(wstring uid) {
  BOOST_LOG_TRIVIAL(info) << "Iterating Original Works For UserID:" << uid
                          << endl;
  auto l = core.timeline_getUserPostTimeLine(uid, downloadCallback);
  BOOST_LOG_TRIVIAL(info) << "Found " << l.size()
                          << " Original Works For UserID:" << uid << endl;
}
void DownloadUtils::downloadTag(wstring TagName) {
  if (typeFilters.size() == 0) {
    BOOST_LOG_TRIVIAL(info) << "Iterating Works For Tag:" << TagName << endl;
    auto coser = core.circle_itemrecenttags(TagName, L"all", downloadCallback);
    BOOST_LOG_TRIVIAL(info)
        << "Found " << coser.size() << " Works For Tag:" << TagName << endl;
  } else {
    web::json::value rep = core.tag_status(TagName);
    if (rep.is_null()) {
      BOOST_LOG_TRIVIAL(error)
          << "Status for Tag:" << TagName << " is null" << endl;
      return;
    }
    int foo = rep[L"data"][L"tag_id"].as_integer();
    wstring circle_id = to_wstring(foo);
    web::json::value FilterList =
        core.circle_filterlist(circle_id, Core::CircleType::Tag, TagName);
    if (FilterList.is_null()) {
      BOOST_LOG_TRIVIAL(error)
          << "FilterList For Tag:" << TagName << " is null";
      return;
    }
    FilterList = FilterList[L"data"];
    for (web::json::value j : FilterList.as_array()) {
      wstring name = j[L"name"].as_string();
      int id = 0;
      wstring idstr = j[L"id"].as_string();
      if (idstr != L"") {
        id = std::stoi(idstr);
      }
      if (find(typeFilters.begin(), typeFilters.end(), name) !=
          typeFilters.end()) {
        if (id >= 1 && id <= 3) { // First class
          BOOST_LOG_TRIVIAL(info) << "Iterating Works For Tag:" << TagName
                                  << " and Filter:" << name << endl;
          auto foo = core.search_item_bytag(
              {name}, static_cast<Core::PType>(id), downloadCallback);
          BOOST_LOG_TRIVIAL(info)
              << "Found " << foo.size() << " Works For Tag:" << TagName
              << " and Filter:" << name << endl;
        } else {
          BOOST_LOG_TRIVIAL(info) << "Iterating Works For Tag:" << TagName
                                  << " and Filter:" << name << endl;
          auto foo = core.search_item_bytag({TagName, name}, Core::PType::Undef,
                                            downloadCallback);
          BOOST_LOG_TRIVIAL(info)
              << "Found " << foo.size() << " Works For Tag:" << TagName
              << " and Filter:" << name << endl;
        }
      }
    }
  }
}
void DownloadUtils::downloadGroupID(wstring gid) {
  BOOST_LOG_TRIVIAL(info) << "Iterating Works For GroupID:" << gid << endl;
  auto l = core.group_listPosts(gid, downloadCallback);
  BOOST_LOG_TRIVIAL(info) << "Found " << l.size()
                          << " Works For GroupID:" << gid << endl;
}
void DownloadUtils::downloadUserLiked(wstring uid) {
  if (uid == core.UID) {
    BOOST_LOG_TRIVIAL(info)
        << "Iterating Liked Works For UserID:" << uid << endl;
    auto l = core.space_getUserLikeTimeLine(uid, [&](web::json::value j) {
      this->downloadFromAbstractInfo(j, false);
      return true;
    });
    BOOST_LOG_TRIVIAL(info)
        << "Found " << l.size() << " Liked Works For UserID:" << uid << endl;
    return;
  }
  BOOST_LOG_TRIVIAL(info) << "Iterating Liked Works For UserID:" << uid << endl;
  auto l = core.space_getUserLikeTimeLine(uid, downloadCallback);
  BOOST_LOG_TRIVIAL(info) << "Found " << l.size()
                          << " Liked Works For UserID:" << uid << endl;
}
void DownloadUtils::downloadItemID(wstring item_id) {
  if (stop) {
    return;
  }
  boost::asio::post(*queryThread, [=]() {
    if (stop) {
      return;
    }
    boost::this_thread::interruption_point();
    optional<DownloadUtils::Info> detail;
    if (useCachedInfo) {
      detail = loadInfo(item_id);
    }
    if (detail.has_value() == false) {
      boost::this_thread::interruption_point();
      detail.emplace(
          canonicalizeRawServerDetail(core.item_detail(item_id)[L"data"]));
      boost::this_thread::interruption_point();
      if (detail.has_value() == false) {
        BOOST_LOG_TRIVIAL(error) << "Querying detail for item_id:" << item_id
                                 << " results in null" << endl;
      }
      try {
        downloadFromInfo(*detail, enableFilter);
      } catch (boost::thread_interrupted) {
        BOOST_LOG_TRIVIAL(debug)
            << "Cancelling Thread:" << boost::this_thread::get_id() << endl;
        return;
      } catch (const exception &exc) {
        BOOST_LOG_TRIVIAL(error)
            << "Downloading from Info:" << std::get<1>(*detail)
            << " Raised Exception:" << exc.what() << endl;
      }
    } else {
      try {
        downloadFromInfo(*detail, enableFilter);
      } catch (boost::thread_interrupted) {
        BOOST_LOG_TRIVIAL(debug)
            << "Cancelling Thread:" << boost::this_thread::get_id() << endl;
        return;
      } catch (const exception &exc) {
        BOOST_LOG_TRIVIAL(error)
            << "Downloading from Info:" << std::get<1>(*detail)
            << " Raised Exception:" << exc.what() << endl;
      }
    }
  });
}
void DownloadUtils::downloadHotTags(wstring TagName, unsigned int cnt) {
  unsigned int c = 0;
  core.circle_itemhottags(TagName, [&](web::json::value j) {
    this->downloadFromAbstractInfo(j, enableFilter);
    c++;
    if (c < cnt) {
      return true;
    }
    return false;
  });
}
void DownloadUtils::downloadWorkID(wstring item) {
  if (typeFilters.size() == 0) {
    BOOST_LOG_TRIVIAL(info) << "Iterating Works For WorkID:" << item << endl;
    auto l = core.circle_itemRecentWorks(item, downloadCallback);
    BOOST_LOG_TRIVIAL(info)
        << "Found " << l.size() << " Works For WorkID:" << item << endl;
  } else {
    web::json::value rep = core.core_status(item);
    if (rep.is_null()) {
      BOOST_LOG_TRIVIAL(error)
          << "Status for WorkID:" << item << " is null" << endl;
      return;
    }
    wstring WorkName = rep[L"data"][L"real_name"].as_string();
    web::json::value FilterList =
        core.circle_filterlist(item, Core::CircleType::Work, WorkName);
    if (FilterList.is_null()) {
      BOOST_LOG_TRIVIAL(error)
          << "FilterList For WorkID:" << item << " is null";
    } else {
      FilterList = FilterList[L"data"];
    }
    for (web::json::value j : FilterList.as_array()) {
      wstring name = j[L"name"].as_string();
      int id = 0;
      wstring idstr = j[L"id"].as_string();
      if (idstr != L"") {
        id = std::stoi(idstr);
      }
      if (find(typeFilters.begin(), typeFilters.end(), name) !=
          typeFilters.end()) {
        if (id >= 1 && id <= 3) { // First class
          BOOST_LOG_TRIVIAL(info) << "Iterating Works For WorkID:" << item
                                  << " and Filter:" << name << endl;
          auto foo = core.search_item_bytag(
              {name}, static_cast<Core::PType>(id), downloadCallback);
          BOOST_LOG_TRIVIAL(info)
              << "Found " << foo.size() << " Works For WorkID:" << item
              << " and Filter:" << name << endl;
        } else {
          BOOST_LOG_TRIVIAL(info) << "Iterating Works For WorkID:" << item
                                  << " and Filter:" << name << endl;
          auto foo = core.search_item_bytag(
              {WorkName, name}, Core::PType::Undef, downloadCallback);
          BOOST_LOG_TRIVIAL(info)
              << "Found " << foo.size() << " Works For WorkID:" << item
              << " and Filter:" << name << endl;
        }
      }
    }
  }
}
void DownloadUtils::addTypeFilter(wstring filter) { typeFilters.insert(filter); }
void DownloadUtils::downloadTimeline() {
  BOOST_LOG_TRIVIAL(info) << "Downloading Friend Feed" << endl;
  core.timeline_friendfeed(downloadCallback);
}
void DownloadUtils::join() {
  BOOST_LOG_TRIVIAL(info) << "Joining Query Threads" << endl;
  queryThread->join();
  BOOST_LOG_TRIVIAL(info) << "Joining Download Threads" << endl;
  downloadThread->join();
}
void DownloadUtils::downloadHotWorks(std::wstring id, unsigned int cnt) {
  unsigned int c = 0;
  core.circle_itemhotworks(id, [&](web::json::value j) {
    this->downloadFromAbstractInfo(j, enableFilter);
    c++;
    if (c < cnt) {
      return true;
    }
    return false;
  });
}
DownloadUtils::~DownloadUtils() {
  stop = true;
  /*
   As long as database is commited properly, everything else could be
   just killed. Boost's thread_pool implementation doesn't provide access to
   underlying boost::thread_group object
   */
  BOOST_LOG_TRIVIAL(info) << "Canceling Query Threads..." << endl;
  queryThread->stop();
  delete queryThread;
  queryThread = nullptr;
  BOOST_LOG_TRIVIAL(info) << "Canceling Download Threads..." << endl;
  downloadThread->stop();
  delete downloadThread;
  downloadThread = nullptr;
  {
    BOOST_LOG_TRIVIAL(info) << "Waiting Database Lock" << endl;
    std::lock_guard<mutex> guard(dbLock);
    BOOST_LOG_TRIVIAL(info) << "Saving Filters..." << endl;
    delete filter;
    filter = nullptr;
  }
}

} // namespace BCY

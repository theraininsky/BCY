#include "BCY/DownloadUtils.hpp"
#include "BCY/Base64.h"
#include "BCY/Utils.hpp"
#include <boost/lexical_cast.hpp>
#include <boost/lockfree/stack.hpp>
#include <boost/log/trivial.hpp>
#include <boost/thread.hpp>
#include <cpprest/http_client.h>
#include <execinfo.h>
#include <fstream>
#include <regex>
#ifdef __APPLE__
#define BOOST_STACKTRACE_GNU_SOURCE_NOT_REQUIRED
#endif
#include <boost/exception/all.hpp>
#include <boost/stacktrace.hpp>
using namespace CryptoPP;
namespace fs = boost::filesystem;
using namespace boost::asio;
using namespace std;
using namespace SQLite;
namespace keywords = boost::log::keywords;
namespace expr = boost::log::expressions;
static const vector<string> InfoKeys = {"item_id", "uid"};
namespace BCY {
DownloadUtils::DownloadUtils(string PathBase, int queryThreadCount,
                             int downloadThreadCount, string Path) {

  saveRoot = PathBase;
  fs::path dir(PathBase);
  fs::path file("BCYInfo.db");
  fs::path full_path = dir / file;
  if (Path != "") {
    DBPath = Path;
  } else {
    DBPath = full_path.string();
  }
  Database DB(DBPath, SQLite::OPEN_READWRITE | OPEN_CREATE);
  if (queryThreadCount == -1) {
    queryThreadCount = std::thread::hardware_concurrency() / 2;
  }
  if (downloadThreadCount == -1) {
    downloadThreadCount = std::thread::hardware_concurrency() / 2;
  }

  queryThread = new thread_pool(queryThreadCount);
  downloadThread = new thread_pool(downloadThreadCount);
  std::lock_guard<mutex> guard(dbLock);
  DB.exec("CREATE TABLE IF NOT EXISTS UserInfo (uid INTEGER,UserName "
          "STRING,UNIQUE(uid) ON CONFLICT IGNORE)");
  DB.exec("CREATE TABLE IF NOT EXISTS EventInfo (event_id INTEGER,etime "
          "INTEGER,stime INTEGER,cover STRING,intro STRING,Info "
          "STRING,UNIQUE(event_id) ON CONFLICT IGNORE)");
  DB.exec("CREATE TABLE IF NOT EXISTS GroupInfo (gid INTEGER,GroupName "
          "STRING,UNIQUE(gid) ON CONFLICT IGNORE)");
  DB.exec("CREATE TABLE IF NOT EXISTS WorkInfo (uid INTEGER DEFAULT 0,Title "
          "STRING NOT NULL DEFAULT '',item_id INTEGER DEFAULT 0,Info STRING "
          "NOT NULL DEFAULT '',Tags STRING NOT NULL DEFAULT '[]',UNIQUE "
          "(item_id) ON CONFLICT REPLACE)");
  DB.exec("CREATE TABLE IF NOT EXISTS PyBCY (Key STRING DEFAULT '',Value "
          "STRING NOT NULL DEFAULT '',UNIQUE(Key) ON CONFLICT IGNORE)");
  DB.exec("PRAGMA journal_mode=WAL;");
  filter = new DownloadFilter(DBPath);
  // Create Download Temp First
  boost::system::error_code ec;
  fs::path TempPath = fs::path(PathBase) / fs::path("DownloadTemp");
  fs::create_directories(TempPath, ec);
}
void DownloadUtils::downloadFromAbstractInfo(web::json::value AbstractInfo) {
  downloadFromAbstractInfo(AbstractInfo, this->enableFilter);
}
void DownloadUtils::downloadFromAbstractInfo(web::json::value AbstractInfo,
                                             bool runFilter) {
  if (stop) {
    return;
  }
  boost::asio::post(*queryThread, [=]() {
    if (stop) {
      return;
    }
    boost::this_thread::interruption_point();
    try {
      if (runFilter == false || filter->shouldBlockAbstract(
                                    AbstractInfo.at("item_detail")) == false||filter->shouldBlockTags(AbstractInfo.at("item_detail"),"",ensure_string(AbstractInfo.at("item_detail").at("item_id")))) {
        boost::this_thread::interruption_point();
        web::json::value detail = loadInfo(
            ensure_string(AbstractInfo.at("item_detail").at("item_id")));
        boost::this_thread::interruption_point();
        if (detail.is_null()) {
          boost::this_thread::interruption_point();
          string item_id =
              ensure_string(AbstractInfo.at("item_detail").at("item_id"));
          detail = core.item_detail(item_id)["data"];
          boost::this_thread::interruption_point();
          try {
            downloadFromInfo(detail, true, item_id, runFilter);
          } catch (boost::thread_interrupted) {
            BOOST_LOG_TRIVIAL(debug)
                << "Cancelling Thread:" << boost::this_thread::get_id() << endl;
            return;
          } catch (const exception &exc) {
            BOOST_LOG_TRIVIAL(error)
                << "Verifying from Info:" << detail.serialize()
                << " Raised Exception:" << exc.what() << endl;
          }
        } else {
          try {
            downloadFromInfo(
                detail, false,
                ensure_string(AbstractInfo.at("item_detail").at("item_id")),
                runFilter);
          } catch (boost::thread_interrupted) {
            BOOST_LOG_TRIVIAL(debug)
                << "Cancelling Thread:" << boost::this_thread::get_id() << endl;
            return;
          } catch (const exception &exc) {
            BOOST_LOG_TRIVIAL(error)
                << "Verifying from Info:" << detail.serialize()
                << " Raised Exception:" << exc.what() << endl;
          }
        }
      }
    } catch (exception &exp) {
      BOOST_LOG_TRIVIAL(error)
          << exp.what() << __FILE__ << ":" << __LINE__ << endl
          << AbstractInfo.serialize() << endl;
std::cerr << boost::stacktrace::stacktrace() << '\n';
    }


  });
}
string DownloadUtils::loadOrSaveGroupName(string name, string GID) {
  std::lock_guard<mutex> guard(dbLock);
  Database DB(DBPath, SQLite::OPEN_READWRITE);
  Statement Q(DB, "SELECT GroupName FROM GroupInfo WHERE gid=(?)");
  Q.bind(1, GID);
  Q.executeStep();
  if (Q.hasRow()) {
    return Q.getColumn(0).getString();
  } else {
    Statement insertQuery(
        DB, "INSERT INTO GroupInfo (gid, GroupName) VALUES (?,?)");
    insertQuery.bind(1, GID);
    insertQuery.bind(2, name);
    insertQuery.executeStep();
    return name;
  }
}
string DownloadUtils::loadTitle(string title, web::json::value Inf) {
  // return title;
  vector<string> keys;
  vector<string> vals;
  stringstream query;
  vector<string> tmps;
  query << "SELECT Title FROM WorkInfo WHERE ";
  for (string K : InfoKeys) {
    if (Inf.has_field(K)) {
      tmps.push_back(K + "=(?)");
      keys.push_back(K);
      string V = ensure_string(Inf[K]);
      vals.push_back(V);
    }
  }
  query << ::BCY::join(tmps.begin(), tmps.end(), " AND ");
  std::lock_guard<mutex> guard(dbLock);
  Database DB(DBPath, OPEN_READONLY);
  Statement Q(DB, query.str());
  for (decltype(keys.size()) i = 0; i < keys.size(); i++) {
    Q.bind(i + 1, vals[i]);
  }
  boost::this_thread::interruption_point();
  Q.executeStep();
  if (Q.hasRow()) {
    string T = Q.getColumn("Title").getString();
    if (T != "") {
      return T;
    }
  }
  return title;
}
void DownloadUtils::saveInfo(string title, web::json::value Inf) {
  // return;
  vector<string> keys;
  vector<string> vals;
  stringstream query;
  vector<string> tmps;
  query << "INSERT OR REPLACE INTO WorkInfo (";
  for (string K : InfoKeys) {
    if (Inf.has_field(K)) {
      tmps.push_back("(?)");
      keys.push_back(K);
      vals.push_back(ensure_string(Inf[K]));
    }
  }
  keys.push_back("Tags");
  tmps.push_back("(?)");
  if (Inf.has_field("post_tags")) {
    vector<web::json::value> tagsB; // Inner Param
  for (web::json::value tagD : Inf["post_tags"].as_array()) {
    string tag = tagD["tag_name"].as_string();
      tagsB.push_back(web::json::value(tag));
  }
    web::json::value arr=web::json::value::array(tagsB);
    vals.push_back(arr.serialize());
  } else {
    vals.push_back("[]");
  }
  keys.push_back("Info");
  tmps.push_back("(?)");
  //Strip duplicate/useless info key/val pairs
#define StripKV(name) \
  if(Inf.has_field(name)){ \
    Inf.erase(name); \
  }

  StripKV("recommend_rela")
  StripKV("post_tags")
  StripKV("user_favored")
  StripKV("user_liked")
  StripKV("view_count")
#undef StripKV

  vals.push_back(Inf.serialize());

  keys.push_back("Title");
  tmps.push_back("(?)");
  vals.push_back(title);
  query << ::BCY::join(keys.begin(), keys.end(), ",") << ") VALUES ("
        << ::BCY::join(tmps.begin(), tmps.end(), ",") << ")";
  std::lock_guard<mutex> guard(dbLock);
  Database DB(DBPath, SQLite::OPEN_READWRITE);
  Statement Q(DB, query.str());
  for (decltype(tmps.size()) i = 0; i < tmps.size(); i++) {
    Q.bind(i + 1, vals[i]);
  }
  Q.executeStep();
}
web::json::value DownloadUtils::loadInfo(string item_id) {
  std::lock_guard<mutex> guard(dbLock);
  Database DB(DBPath, SQLite::OPEN_READONLY);
  Statement Q(DB, "SELECT Info FROM WorkInfo WHERE item_id=?");
  Q.bind(1, item_id);
  Q.executeStep();
  if (Q.hasRow()) {
    return web::json::value::parse(Q.getColumn(0).getString());
  } else {
    return web::json::value();
  }
}
void DownloadUtils::downloadEvent(string event_id) {
  //  DB.exec("CREATE TABLE IF NOT EXISTS EventInfo (event_id INTEGER,etime
  //  INTEGER,stime INTEGER,cover STRING,intro STRING,UNIQUE(event_id) ON
  //  CONFLICT IGNORE)");
  web::json::value det = loadEventInfo(event_id);
  if (det.is_null()) {
    det = core.event_detail(event_id)["data"];
    insertEventInfo(det);
  }
  core.event_listPosts(event_id, Order::Index, downloadCallback);
}
web::json::value DownloadUtils::loadEventInfo(string event_id) {
  std::lock_guard<mutex> guard(dbLock);
  Database DB(DBPath, SQLite::OPEN_READWRITE);
  Statement Q(DB, "SELECT Info FROM EventInfo WHERE event_id=?");
  Q.bind(1, event_id);
  Q.executeStep();
  if (Q.hasRow()) {
    return web::json::value::parse(Q.getColumn(0).getString());
  } else {
    return web::json::value();
  }
}
void DownloadUtils::insertEventInfo(web::json::value Inf) {
  std::lock_guard<mutex> guard(dbLock);
  Database DB(DBPath, SQLite::OPEN_READWRITE);
  Statement insertQuery(
      DB, "INSERT INTO EventInfo (event_id,etime,stime,cover,intro,Info) "
          "VALUES (?,?,?,?,?,?)");
  insertQuery.bind(1, Inf["event_id"].as_integer());
  insertQuery.bind(2, Inf["etime"].as_integer());
  insertQuery.bind(3, Inf["stime"].as_integer());
  insertQuery.bind(4, Inf["cover"].as_string());
  insertQuery.bind(5, Inf["intro"].as_string());
  insertQuery.bind(6, Inf.serialize());
  insertQuery.executeStep();
}
void DownloadUtils::downloadFromInfo(web::json::value Inf, bool save,
                                     string item_id_arg, bool runFilter) {
  if (!Inf.is_object()) {
    BOOST_LOG_TRIVIAL(error)
        << Inf.serialize() << " is not valid Detail Info For Downloading"
        << endl;
    return;
  }

  if (item_id_arg != "" && runFilter) {
    // Not Called by the AbstractInfo Worker
    // Need to run our own filter process
    string tagStr;
    if (Inf.has_field("post_tags")==false){
      //Extract from DB
      std::lock_guard<mutex> guard(dbLock);
      Database DB(DBPath, SQLite::OPEN_READONLY);
      Statement Q(DB, "SELECT Tags FROM WorkInfo WHERE item_id=" + item_id_arg);
      Q.executeStep();
      tagStr=Q.getColumn(0).getString();
    }
    if (filter->shouldBlockDetail(Inf, item_id_arg) ||
        filter->shouldBlockAbstract(Inf, item_id_arg)||filter->shouldBlockTags(Inf,tagStr,item_id_arg)) {
      return;
    }
  }
  string UID = ensure_string(Inf["uid"]);
  // tyvm cunts at ByteDance
  string item_id = ensure_string(Inf["item_id"]);
  if (item_id == "") {
    if (item_id_arg != "") {
      item_id = item_id_arg;
    } else {
      BOOST_LOG_TRIVIAL(error)
          << Inf.serialize() << " doesnt have valid item_id" << endl;
      return;
    }
  }
  string Title = "";
  if (Inf.has_field("title") && Inf["title"].as_string() != "") {
    Title = Inf["title"].as_string();
  } else {
    if (Inf.has_field("post_core") && Inf.at("post_core").has_field("name") &&
        Inf["post_core"]["name"].is_string()) {
      Title = Inf["post_core"]["name"].as_string();
    } else {
      if (Inf.has_field("ud_id")) {
        string val = ensure_string(Inf["ud_id"]);
        Title = "日常-" + val;
      } else if (Inf.has_field("cp_id")) {
        string val = ensure_string(Inf["cp_id"]);
        Title = "Cosplay-" + val;
      } else if (Inf.has_field("dp_id")) {
        string val = ensure_string(Inf["dp_id"]);
        Title = "绘画" + val;
      } else if (Inf.has_field("post_id")) {
        string GID = ensure_string(Inf["group"]["gid"]);
        string GroupName =
            loadOrSaveGroupName(Inf["group"]["name"].as_string(), GID);
        string val = "";
        val = ensure_string(Inf["ud_id"]);
        Title = GroupName + "-" + val;
      } else if (Inf.has_field("item_id")) {
        string val = ensure_string(Inf["item_id"]);
        if (Inf["type"].as_string() == "works") {
          Title = "Cosplay-" + val;
        } else if (Inf["type"].as_string() == "preview") {
          Title = "预告-" + val;
        } else if (Inf["type"].as_string() == "daily") {
          Title = "日常-" + val;
        } else if (Inf["type"].as_string() == "video") {
          Title = "视频-" + val;
        } else {
          Title = val;
        }
      }
    }
  }
  if (Title == "") {
    Title = item_id;
  }
  Title = loadTitle(Title, Inf);
  /*
  Title usually contains UTF8 characters which causes trouble on certain
  platforms. (I'm looking at you Windowshit) Migrate to item_id based ones
  */

  boost::this_thread::interruption_point();
  BOOST_LOG_TRIVIAL(debug) << "Loading Title For:" << Title << endl;
  boost::this_thread::interruption_point();
  if (save) {
    boost::this_thread::interruption_point();
    BOOST_LOG_TRIVIAL(debug) << "Saving Info For: " << Title << endl;
    saveInfo(Title, Inf);
    boost::this_thread::interruption_point();
  }
  fs::path newSavePath = getItemPath(UID, item_id);

  if (Inf.has_field("multi") == false) {
    Inf["multi"] = web::json::value::array();
  }
  if (Inf.has_field("cover")) {
    vector<web::json::value> URLs;
    for (web::json::value item : Inf["multi"].as_array()) {
      URLs.push_back(item);
    }
    web::json::value j;
    j["type"] = web::json::value("image");
    j["path"] = Inf["cover"];
    URLs.emplace_back(j);
    Inf["multi"] = web::json::value::array(URLs);
  }
  if (Inf.has_field("type") && Inf["type"].as_string() == "larticle" &&
      Inf["multi"].size() == 0) {
    vector<web::json::value> URLs;
    regex rgx("<img src=\"(.{80,100})\" alt=");
    string tmpjson = Inf.serialize();
    smatch matches;
    while (regex_search(tmpjson, matches, rgx)) {
      web::json::value j;
      string URL = matches[0];
      j["type"] = web::json::value("image");
      j["path"] = web::json::value(URL);
      URLs.emplace_back(j);
      tmpjson = matches.suffix();
    }
    Inf["multi"] = web::json::value::array(URLs);
  }
  if (Inf.has_field("group") && Inf["group"].has_field("multi")) {
    vector<web::json::value> URLs;
    if (Inf.has_field("multi")) {
      for (web::json::value foo : Inf["multi"].as_array()) {
        URLs.push_back(foo);
      }
    }
    for (web::json::value foo : Inf["group"]["multi"].as_array()) {
      URLs.push_back(foo);
    }
    Inf["multi"] = web::json::value::array(URLs);
  }

  // videoInfo
  if (Inf.has_field("type") && Inf["type"].as_string() == "video" &&
      Inf.has_field("video_info") && downloadVideo == true) {
    string vid = Inf["video_info"]["vid"].as_string();
    boost::this_thread::interruption_point();
    web::json::value F = core.videoInfo(vid);
    boost::this_thread::interruption_point();
    web::json::value videoList = F["data"]["video_list"];
    // Find the most HD one
    int bitrate = 0;
    string videoID = "video_1";
    if (videoList.is_null()) {
      BOOST_LOG_TRIVIAL(error)
          << "Can't query DownloadURL for videoID:" << videoID << endl;
    } else {
      for (auto it = videoList.as_object().cbegin();
           it != videoList.as_object().cend(); ++it) {
        string K = it->first;
        web::json::value V = it->second;
        if (V["bitrate"].as_integer() > bitrate) {
          videoID = K;
        }
      }
      if (videoList.size() > 0) {
        // Videos needs to be manually reviewed before playable
        string URL = "";
        Base64::Decode(videoList[videoID]["main_url"].as_string(), &URL);
        string FileName = vid + ".mp4";
        web::json::value j;
        j["path"] = web::json::value(URL);
        j["FileName"] = web::json::value(FileName);
        vector<web::json::value> URLs;
        for (web::json::value bar : Inf["multi"].as_array()) {
          URLs.push_back(bar);
        }
        URLs.push_back(j);
        Inf["multi"] = web::json::value::array(URLs);
      } else {
        BOOST_LOG_TRIVIAL(info)
            << item_id << " hasn't been reviewed yet and thus not downloadable"
            << endl;
      }
    }
  }

  bool isCompressedInfo = false;
  if (allowCompressed && Inf.has_field("item_id") && Inf.has_field("type")) {
    string item_id = ensure_string(Inf["item_id"]);
    web::json::value covers =
        core.image_postCover(item_id, Inf["type"].as_string())["data"];
    if (!covers.is_null() && covers.has_field("multi")) {
      Inf["multi"] = covers["multi"];
      isCompressedInfo = true;
    }
  }

  if (Inf["multi"].size() > 0) {
    boost::system::error_code ec;
    fs::create_directories(newSavePath, ec);
    if (ec) {
      BOOST_LOG_TRIVIAL(error) << "FileSystem Error: " << ec.message() << "@"
                               << __FILE__ << ":" << __LINE__ << endl;
    }
  } else {
    BOOST_LOG_TRIVIAL(error) << item_id <<" has no item to download"<< endl;
    return;
  }
  vector<web::json::value>
      a2Methods; // Used for aria2 RPC's ``system.multicall``
  for (web::json::value item : Inf["multi"].as_array()) {
    boost::this_thread::interruption_point();
    string URL = item["path"].as_string();
    if (URL.find("http") != 0) {
      // Some old API bug that results in rubbish URL in response
      // Because ByteDance sucks dick
      continue;
    }
    if (URL.length() == 0) {
      continue;
    }
    string origURL = URL;
    string tmp = URL.substr(URL.find_last_of("/"), string::npos);
    if (!isCompressedInfo && tmp.find(".") == string::npos) {
      origURL = URL.substr(0, URL.find_last_of("/"));
    }
    string FileName = "";
    if (!isCompressedInfo) {
      FileName = origURL.substr(origURL.find_last_of("/") + 1);
    } else {
      string URLWithoutQuery = origURL.substr(0, origURL.find_last_of("?"));
      FileName = URLWithoutQuery.substr(URLWithoutQuery.find_last_of("/") + 1);
    }
    if (item.has_field("FileName")) { // Support Video Downloading without
                                      // Introducing Extra
      // Code
      FileName = item["FileName"].as_string();
      origURL = item["path"].as_string();
    }
    fs::path newFilePath = newSavePath / fs::path(FileName);
    if (!newFilePath.has_extension()) {
      newFilePath.replace_extension(".jpg");
    }
    boost::system::error_code ec2;
    auto newa2confPath = fs::path(newFilePath.string() + ".aria2");

    bool shouldDL =
        (!fs::exists(newFilePath, ec2) || fs::exists(newa2confPath, ec2));
    if (shouldDL) {
      if (RPCServer == "") {
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
            auto R = core.GET(origURL);
            boost::this_thread::interruption_point();
            ofstream ofs(newFilePath.string(), ios::binary);
            auto vec = R.extract_vector().get();
            ofs.write(reinterpret_cast<const char *>(vec.data()), vec.size());
            ofs.close();
            boost::this_thread::interruption_point();
          } catch (const std::exception &exp) {
            BOOST_LOG_TRIVIAL(error)
                << "Download from " << origURL
                << " failed with exception:" << exp.what() << endl;
          }
        });
      } else {
        boost::this_thread::interruption_point();
        web::json::value rpcparams;
        vector<web::json::value> params; // Inner Param
        vector<web::json::value> URLs;
        URLs.push_back(web::json::value(origURL));
        if (secret != "") {
          params.push_back(web::json::value("token:" + secret));
        }
        params.push_back(web::json::value::array(URLs));
        web::json::value options;
        options["dir"] = web::json::value(newSavePath.string());
        options["out"] = web::json::value(newFilePath.filename().string());
        options["auto-file-renaming"] = web::json::value("false");
        options["allow-overwrite"] = web::json::value("false");
        // options["user-agent"] = "bcy 4.3.2 rv:4.3.2.6146 (iPad; iPhone
        // OS 9.3.3; en_US) Cronet";
        string gid = md5(origURL).substr(0, 16);
        options["gid"] = web::json::value(gid);
        params.push_back(options);
        if (item.has_field("FileName")) {
          /*
           Video URLs have a (very short!) valid time window
           so we insert those to the start of download queue
          */
          params.push_back(web::json::value(1));
        }
        rpcparams["params"] = web::json::value::array(params);
        rpcparams["methodName"] = web::json::value("aria2.addUri");
        a2Methods.push_back(rpcparams);
        boost::this_thread::interruption_point();
      }
    }
  }
  if (a2Methods.size() > 0) {
    try {

      web::json::value arg;
      arg["jsonrpc"] = web::json::value("2.0");
      arg["id"] = web::json::value();
      arg["method"] = web::json::value("system.multicall");
      vector<web::json::value> tmp;
      tmp.push_back(web::json::value::array(a2Methods));
      arg["params"] = web::json::value::array(tmp);
      web::http::client::http_client client(RPCServer);
      web::json::value rep =
          client.request(web::http::methods::POST, U("/"), arg)
              .get()
              .extract_json(true)
              .get();

      if (rep.has_field("result")) {
        BOOST_LOG_TRIVIAL(debug)
            << item_id
            << " Registered in Aria2 with GID:" << rep["result"].serialize()
            << " Query:" << arg.serialize() << endl;
      } else {
        BOOST_LOG_TRIVIAL(error)
            << item_id << " Failed to Register with Aria2. Response:"
            << rep["error"]["message"].as_string()
            << " Query:" << arg.serialize() << endl;
      }
    } catch (const std::exception &exp) {
      BOOST_LOG_TRIVIAL(error)
          << "Posting to Aria2 Error:" << exp.what() << endl;
    }
  }
}
void DownloadUtils::verifyUID(string UID, bool reverse) {
  verify("WHERE uid=?", {UID}, reverse);
}
void DownloadUtils::verifyTag(string Tag, bool reverse) {
  verify("WHERE Tags LIKE ?", {"%" + Tag + "%"}, reverse);
}
void DownloadUtils::unlikeCached() {
  vector<web::json::value> Liked = core.space_getUserLikeTimeLine(core.UID);
  BOOST_LOG_TRIVIAL(info) << "Found " << Liked.size() << " Liked Works" << endl;
  thread_pool t(16);
  for (web::json::value j : Liked) {
    string item_id = ensure_string(j["item_detail"]["item_id"]);
    boost::asio::post(t, [=] {
      if (!loadInfo(item_id).is_null()) {
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
void DownloadUtils::verify(string condition, vector<string> args,
                           bool reverse) {
  BOOST_LOG_TRIVIAL(info) << "Verifying..." << endl;
  map<string, web::json::value> Info;
  vector<string> Keys;
  BOOST_LOG_TRIVIAL(info) << "Collecting Cached Infos" << endl;
  {
    std::lock_guard<mutex> guard(dbLock);
    Database DB(DBPath, SQLite::OPEN_READONLY);
    Statement Q(DB, "SELECT item_id,Info,Tags FROM WorkInfo " + condition);
    for (decltype(args.size()) i = 1; i <= args.size(); i++) {
      Q.bind(i, args[i - 1]);
    }
    while (Q.executeStep()) {
      string item_id = Q.getColumn(0).getString();
      string InfoStr = Q.getColumn(1).getString();
      string TagStr = Q.getColumn(2).getString();
      try {
        web::json::value j = web::json::value::parse(InfoStr);
        if (item_id == "0" || item_id == "") {
          BOOST_LOG_TRIVIAL(error)
              << InfoStr << " Doesn't Have Valid item_id" << endl;
          continue;
        }
          try{
              if(filter->shouldBlockTags(j,TagStr,item_id)){
                  continue;
              }
          }
          catch (exception &exp) {
              BOOST_LOG_TRIVIAL(info)
              << exp.what() << __FILE__ << ":" << __LINE__ << endl;
              std::cerr << boost::stacktrace::stacktrace() << '\n';

          }

        Info[item_id] = j;
        Keys.push_back(item_id);
        if (Info.size() % 1000 == 0) {
          BOOST_LOG_TRIVIAL(info) << Info.size() << " Cache Loaded" << endl;
        }
      } catch (exception &exp) {
        BOOST_LOG_TRIVIAL(info)
            << exp.what() << __FILE__ << ":" << __LINE__ << endl;
          std::cerr << boost::stacktrace::stacktrace() << '\n';

      }
    }
  }
  BOOST_LOG_TRIVIAL(info) << "Found " << Info.size() << " Cached Info" << endl;
  if (reverse == false) {
    for (decltype(Keys.size()) i = 0; i < Keys.size(); i++) {
      string K = Keys[i];
      web::json::value &j = Info[K];
      if (i % 1000 == 0) {
        BOOST_LOG_TRIVIAL(info)
            << "Remaining Caches to Process:" << Info.size() - i << endl;
      }
      boost::asio::post(*queryThread, [=]() {
        if (stop) {
          return;
        }
        try {
          downloadFromInfo(j, false, K, enableFilter);
        } catch (boost::thread_interrupted) {
          BOOST_LOG_TRIVIAL(debug)
              << "Cancelling Thread:" << boost::this_thread::get_id() << endl;
          return;
        } catch (const exception &exc) {
          BOOST_LOG_TRIVIAL(error)
              << "Verifying from Info:" << j.serialize()
              << " Raised Exception:" << exc.what() << endl;
        }
      });
    }
  } else {
    for (int i = Keys.size() - 1; i >= 0; i--) {
      string K = Keys[i];
      web::json::value &j = Info[K];
      if (i % 1000 == 0) {
        BOOST_LOG_TRIVIAL(info) << "Remaining Caches to Process:" << i << endl;
      }
      boost::asio::post(*queryThread, [=]() {
        if (stop) {
          return;
        }
        try {
          downloadFromInfo(j, false, K, enableFilter);
        } catch (boost::thread_interrupted) {
          BOOST_LOG_TRIVIAL(debug)
              << "Cancelling Thread:" << boost::this_thread::get_id() << endl;
          return;
        } catch (const exception &exc) {
          BOOST_LOG_TRIVIAL(error)
              << "Verifying from Info:" << j.serialize()
              << " Raised Exception:" << exc.what() << endl;
        }
      });
    }
  }
}

void DownloadUtils::cleanByFilter() {
  vector<std::tuple<string, string, web::json::value>> Infos;
  BOOST_LOG_TRIVIAL(info) << "Collecting Cached Infos" << endl;
  {
    std::lock_guard<mutex> guard(dbLock);
    Database DB(DBPath, SQLite::OPEN_READONLY);
    Statement Q(DB, "SELECT uid,item_id,Info FROM WorkInfo");
    while (Q.executeStep()) {
      string uid = Q.getColumn(0).getString();
      string item_id = Q.getColumn(1).getString();
      string InfoStr = Q.getColumn(2).getString();
      try {
        web::json::value j = web::json::value::parse(InfoStr);
        if (item_id == "0" || item_id == "") {
          BOOST_LOG_TRIVIAL(error)
              << InfoStr << " Doesn't Have Valid item_id" << endl;
          continue;
        }
        Infos.push_back(std::make_tuple(uid, item_id, j));

        if (Infos.size() % 1000 == 0) {
          BOOST_LOG_TRIVIAL(info) << Infos.size() << " Cache Loaded" << endl;
        }
      } catch (exception &exp) {
        BOOST_LOG_TRIVIAL(info)
            << exp.what() << __FILE__ << ":" << __LINE__ << endl;
          std::cerr << boost::stacktrace::stacktrace() << '\n';

      }
    }
  }
  BOOST_LOG_TRIVIAL(info) << "Processing..." << endl;
  thread_pool t(16);
  std::atomic<unsigned long long> i = 0;
  for (std::tuple<string, string, web::json::value> tup : Infos) {
    string uid = std::get<0>(tup);
    string item_id = std::get<1>(tup);
    web::json::value Inf = std::get<2>(tup);
    boost::asio::post(t, [=, &i]() {
      boost::system::error_code ec;
      fs::path WorkPath = getItemPath(uid, item_id);
      bool isDirec = is_directory(WorkPath, ec);
      if (isDirec) {
        if (filter->shouldBlockDetail(Inf, item_id) ||
            filter->shouldBlockAbstract(Inf, item_id)) {
          fs::remove_all(WorkPath, ec);
          BOOST_LOG_TRIVIAL(debug) << "Removed " << WorkPath.string() << endl;
        }
      }
      i++;
      if (i % 1000 == 0) {
        BOOST_LOG_TRIVIAL(info) << i << " Processed" << endl;
      }
    });
  }
  t.join();
}
void DownloadUtils::cleanUID(string UID) {
  BOOST_LOG_TRIVIAL(debug) << "Cleaning up UID:" << UID << endl;
  boost::system::error_code ec;
  fs::path UserPath = getUserPath(UID);
  bool isDirec = is_directory(UserPath, ec);
  if (isDirec) {
    fs::remove_all(UserPath, ec);
    BOOST_LOG_TRIVIAL(debug) << "Removed " << UserPath.string() << endl;
  }

  std::lock_guard<mutex> guard(dbLock);
  Database DB(DBPath, SQLite::OPEN_READWRITE);
  Statement Q(DB, "DELETE FROM WorkInfo WHERE UID=" + UID);
  Q.executeStep();
}
fs::path DownloadUtils::getUserPath(string UID) {
  while (UID.length() < 3) {
    UID = "0" + UID;
  }
  string L1Path = string(1, UID[0]);
  string L2Path = string(1, UID[1]);
  fs::path UserPath =
      fs::path(saveRoot) / fs::path(L1Path) / fs::path(L2Path) / fs::path(UID);
  return UserPath;
}
fs::path DownloadUtils::getItemPath(string UID, string item_id) {
  return getUserPath(UID) / fs::path(item_id);
}
void DownloadUtils::cleanTag(string Tag) {
  BOOST_LOG_TRIVIAL(info) << "Cleaning up Tag:" << Tag << endl;
  std::lock_guard<mutex> guard(dbLock);
  Database DB(DBPath, SQLite::OPEN_READWRITE);
  Statement Q(DB,
              "SELECT UID,Title,Info,item_id FROM WorkInfo WHERE Tags Like ?");
  Q.bind(1, "%%\"" + Tag + "\"%%");
  while (Q.executeStep()) {
    string UID = Q.getColumn(0).getString();
    string Title = Q.getColumn(1).getString();
    string Info = Q.getColumn(2).getString();
    string item_id = Q.getColumn(3).getString();
    if (item_id == "" || item_id == "0") {
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
      string UID = filter->UIDList[i].as_string();
      BOOST_LOG_TRIVIAL(debug) << "Removing UID: " << UID << endl;
      cleanUID(UID);
    });
  }
  // vector<string> Infos;
  for (decltype(filter->TagList.size()) i = 0; i < filter->TagList.size();
       i++) {
    boost::asio::post(t, [=]() {
      string Tag = filter->TagList[i].as_string();
      BOOST_LOG_TRIVIAL(debug) << "Removing Tag: " << Tag << endl;
      cleanTag(Tag);
    });
  }
  t.join();
}
string DownloadUtils::md5(string &str) {
  string digest;
  Weak::MD5 md5;
  StringSource(str, true,
               new HashFilter(md5, new HexEncoder(new StringSink(digest))));
  return digest;
}
void DownloadUtils::downloadLiked() {
  if (core.UID != "") {
    downloadUserLiked(core.UID);
  } else {
    BOOST_LOG_TRIVIAL(error)
        << "Not Logged In. Can't Download Liked Work" << endl;
  }
}
void DownloadUtils::downloadSearchKeyword(string KW) {
  BOOST_LOG_TRIVIAL(info) << "Iterating Searched Works For Keyword:" << KW
                          << endl;
  auto l = core.search(KW, SearchType::Content, downloadCallback);

  BOOST_LOG_TRIVIAL(info) << "Found " << l.size()
                          << " Searched Works For Keyword:" << KW << endl;
}
void DownloadUtils::downloadUser(string uid) {
  BOOST_LOG_TRIVIAL(info) << "Iterating Original Works For UserID:" << uid
                          << endl;
  auto l = core.timeline_getUserPostTimeLine(uid, downloadCallback);
  BOOST_LOG_TRIVIAL(info) << "Found " << l.size()
                          << " Original Works For UserID:" << uid << endl;
}
void DownloadUtils::downloadTag(string TagName) {
  if (typeFilters.size() == 0) {
    BOOST_LOG_TRIVIAL(info) << "Iterating Works For Tag:" << TagName << endl;
    auto coser = core.circle_itemrecenttags(TagName, "all", downloadCallback);
    BOOST_LOG_TRIVIAL(info)
        << "Found " << coser.size() << " Works For Tag:" << TagName << endl;
  } else {
    web::json::value rep = core.tag_status(TagName);
    if (rep.is_null()) {
      BOOST_LOG_TRIVIAL(error)
          << "Status for Tag:" << TagName << " is null" << endl;
      return;
    }
    int foo = rep["data"]["tag_id"].as_integer();
    string circle_id = to_string(foo);
    web::json::value FilterList =
        core.circle_filterlist(circle_id, CircleType::Tag, TagName);
    if (FilterList.is_null()) {
      BOOST_LOG_TRIVIAL(error)
          << "FilterList For Tag:" << TagName << " is null";
      return;
    }
    FilterList = FilterList["data"];
    for (web::json::value j : FilterList.as_array()) {
      string name = j["name"].as_string();
      int id = 0;
      string idstr = j["id"].as_string();
      if (idstr != "") {
        id = std::stoi(idstr);
      }
      if (find(typeFilters.begin(), typeFilters.end(), name) !=
          typeFilters.end()) {
        if (id >= 1 && id <= 3) { // First class
          BOOST_LOG_TRIVIAL(info) << "Iterating Works For Tag:" << TagName
                                  << " and Filter:" << name << endl;
          auto foo = core.search_item_bytag({name}, static_cast<PType>(id),
                                            downloadCallback);
          BOOST_LOG_TRIVIAL(info)
              << "Found " << foo.size() << " Works For Tag:" << TagName
              << " and Filter:" << name << endl;
        } else {
          BOOST_LOG_TRIVIAL(info) << "Iterating Works For Tag:" << TagName
                                  << " and Filter:" << name << endl;
          auto foo = core.search_item_bytag({TagName, name}, PType::Undef,
                                            downloadCallback);
          BOOST_LOG_TRIVIAL(info)
              << "Found " << foo.size() << " Works For Tag:" << TagName
              << " and Filter:" << name << endl;
        }
      }
    }
  }
}
void DownloadUtils::downloadGroupID(string gid) {
  BOOST_LOG_TRIVIAL(info) << "Iterating Works For GroupID:" << gid << endl;
  auto l = core.group_listPosts(gid, downloadCallback);
  BOOST_LOG_TRIVIAL(info) << "Found " << l.size()
                          << " Works For GroupID:" << gid << endl;
}
void DownloadUtils::downloadUserLiked(string uid) {
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
void DownloadUtils::downloadItemID(string item_id) {
  if (stop) {
    return;
  }
  boost::asio::post(*queryThread, [=]() {
    if (stop) {
      return;
    }
    boost::this_thread::interruption_point();
    web::json::value detail = web::json::value::null();
    if (useCachedInfo) {
      detail = loadInfo(item_id);
    }
    if (detail.is_null()) {
      boost::this_thread::interruption_point();
      detail = core.item_detail(item_id);
      boost::this_thread::interruption_point();
      if (detail.is_null()) {
        BOOST_LOG_TRIVIAL(error) << "Querying detail for item_id:" << item_id
                                 << " results in null" << endl;
      } else {
        detail = detail["data"];
      }
      try {
        downloadFromInfo(detail, true, item_id, enableFilter);
      } catch (boost::thread_interrupted) {
        BOOST_LOG_TRIVIAL(debug)
            << "Cancelling Thread:" << boost::this_thread::get_id() << endl;
        return;
      } catch (const exception &exc) {
        BOOST_LOG_TRIVIAL(error)
            << "Downloading from Info:" << detail.serialize()
            << " Raised Exception:" << exc.what() << endl;
      }
    } else {
      try {
        downloadFromInfo(detail, false, item_id, enableFilter);
      } catch (boost::thread_interrupted) {
        BOOST_LOG_TRIVIAL(debug)
            << "Cancelling Thread:" << boost::this_thread::get_id() << endl;
        return;
      } catch (const exception &exc) {
        BOOST_LOG_TRIVIAL(error)
            << "Downloading from Info:" << detail.serialize()
            << " Raised Exception:" << exc.what() << endl;
      }
    }
  });
}
void DownloadUtils::downloadHotTags(string TagName, unsigned int cnt) {
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
void DownloadUtils::downloadWorkID(string item) {
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
    string WorkName = rep["data"]["real_name"].as_string();
    web::json::value FilterList =
        core.circle_filterlist(item, CircleType::Work, WorkName);
    if (FilterList.is_null()) {
      BOOST_LOG_TRIVIAL(error)
          << "FilterList For WorkID:" << item << " is null";
    } else {
      FilterList = FilterList["data"];
    }
    for (web::json::value j : FilterList.as_array()) {
      string name = j["name"].as_string();
      int id = 0;
      string idstr = j["id"].as_string();
      if (idstr != "") {
        id = std::stoi(idstr);
      }
      if (find(typeFilters.begin(), typeFilters.end(), name) !=
          typeFilters.end()) {
        if (id >= 1 && id <= 3) { // First class
          BOOST_LOG_TRIVIAL(info) << "Iterating Works For WorkID:" << item
                                  << " and Filter:" << name << endl;
          auto foo = core.search_item_bytag({name}, static_cast<PType>(id),
                                            downloadCallback);
          BOOST_LOG_TRIVIAL(info)
              << "Found " << foo.size() << " Works For WorkID:" << item
              << " and Filter:" << name << endl;
        } else {
          BOOST_LOG_TRIVIAL(info) << "Iterating Works For WorkID:" << item
                                  << " and Filter:" << name << endl;
          auto foo = core.search_item_bytag({WorkName, name}, PType::Undef,
                                            downloadCallback);
          BOOST_LOG_TRIVIAL(info)
              << "Found " << foo.size() << " Works For WorkID:" << item
              << " and Filter:" << name << endl;
        }
      }
    }
  }
}
void DownloadUtils::addTypeFilter(string filter) { typeFilters.insert(filter); }
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
void DownloadUtils::downloadHotWorks(std::string id, unsigned int cnt) {
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

#include <BCY/Base64.h>
#include <BCY/Core.hpp>
#include <BCY/DownloadFilter.hpp>
#include <BCY/DownloadUtils.hpp>
#include <BCY/Utils.hpp>
#include <algorithm>
#include <boost/algorithm/string.hpp>
#include <boost/filesystem.hpp>
#include <boost/log/core.hpp>
#include <boost/log/expressions.hpp>
#include <boost/log/support/date_time.hpp>
#include <boost/log/trivial.hpp>
#include <boost/log/utility/setup.hpp>
#include <boost/program_options.hpp>
#include <chaiscript/chaiscript.hpp>
#include <chaiscript/utility/json.hpp>
#include <chrono>
#include <cpprest/json.h>
#include <fstream>
#include <random>

#ifdef _DEBUG
#pragma comment (lib, "D:/Repo/BCY/cryptopp/x64/Output/Debug/cryptlib.lib")
#pragma comment (lib, "D:/Repo/BCY/SQLiteCpp/sqlite3/Debug/sqlite3.lib")
#else
#pragma comment (lib, "D:/Repo/BCY/cryptopp/x64/Output/Release/cryptlib.lib")
#pragma comment (lib, "D:/Repo/BCY/SQLiteCpp/sqlite3/Release/sqlite3.lib")
#endif

using namespace BCY;
using namespace std::placeholders;
using namespace std;
using namespace boost;
namespace bfs = boost::filesystem;
namespace po = boost::program_options;
namespace logging = boost::log;
namespace src = boost::log::sources;
namespace sinks = boost::log::sinks;
namespace keywords = boost::log::keywords;
namespace expr = boost::log::expressions;

extern std::wstring_convert<std::codecvt_utf8<wchar_t>> toWstring;
static DownloadUtils *DU = nullptr;
static po::variables_map vm;
static po::positional_options_description pos;
static web::json::value config;
static wstring Prefix =
    L"BCYDownloader"; // After Login we replace this with UserName
typedef std::function<void(vector<wstring>)> commHandle;
static map<string, commHandle> handlers;
static map<string, string> handlerMsgs;
enum class mode: int { Interactive, JSON, Blocker };
void JSONMode();
void blockerMode(vector<wstring>);
void cleanup(int sig) {
  if (DU != nullptr) {
    delete DU;
    DU = nullptr;
  }
  char b[100];
  sprintf(b, "taskkill /PID %d /F", getpid());
  std::system(b);
}
void cleanup2() {
  if (DU != nullptr) {
    delete DU;
    DU = nullptr;
  }
}
void cleanupHandle() { DU->cleanup(); }
static void quit() {
  if (DU != nullptr) {
    delete DU;
    DU = nullptr;
  }
  exit(0);
}
static void liked(wstring UID) {
  if (UID != L"") {
    DU->downloadUserLiked(UID);
  } else {
    DU->downloadLiked();
  }
}
static void unlike() { DU->unlikeCached(); }
static void downloadVideo(bool flag) { DU->downloadVideo = flag; }
static void process() { JSONMode(); }
static void aria2(wstring addr, wstring secret) {
  DU->RPCServer = addr;
  DU->secret = secret;
}
static void init(wstring path, int queryCnt, int downloadCnt, wstring DBPath) {
  if (DU == nullptr) {
    try {
      DU = new DownloadUtils(path, queryCnt, downloadCnt, DBPath);
    } catch (const SQLite::Exception &ex) {
      cout << "Database Error:" << ex.getErrorStr() << endl
           << "Error Code:" << ex.getErrorCode() << endl
           << "Extended Error Code:" << ex.getExtendedErrorCode() << endl;
      abort();
    }
    signal(SIGINT, cleanup);
  } else {
    cout << "Already Initialized" << endl;
  }
}
static void loginWithSKey(wstring uid, wstring SKey) {
  DU->core.loginWithUIDAndSessionKey(uid, SKey);
}
static void hotTags(wstring tagName, unsigned int cnt) {
  DU->downloadHotTags(tagName, cnt);
}
static void hotWorks(wstring id, unsigned int cnt) {
  DU->downloadHotWorks(id, cnt);
}
static void addtype(wstring typ) { DU->addTypeFilter(typ); }
static void user(wstring UID) { DU->downloadUser(UID); }
static void tag(wstring tag) { DU->downloadTag(tag); }
static void work(wstring workid) { DU->downloadWorkID(workid); }
static void item(wstring item_id) { DU->downloadItemID(item_id); }
static void proxy(wstring proxy) { DU->core.proxy = proxy; }
static void group(wstring gid) { DU->downloadGroupID(gid); }
static void cleanup() { DU->cleanup(); }
static void joinHandle() { DU->join(); }
static void toggleFilter(bool stat) { DU->enableFilter = stat; }
static void login(wstring email, wstring password) {
  if (DU->core.UID == L"") {
#ifndef DEBUG
    try {
#endif
      web::json::value Res =
          DU->core.loginWithEmailAndPassword(email, password);
      if (!Res.is_null()) {
        Prefix = Res[L"data"][L"uname"].as_string();
      } else {
        cout << "Login Failed" << endl;
      }
#ifndef DEBUG
    } catch (std::exception &exc) {
      cout << "Exception:" << exc.what() << endl;
    }
#endif
  } else {
    wcout << L"Already logged in as UID:" << DU->core.UID << endl;
  }
}
static void verifyUID(wstring UID, bool reverse) { DU->verifyUID(UID, reverse); }
static void verify(bool reverse) { DU->verify(L"", {}, reverse); }
static void forcequit() {
    char b[100];
    sprintf(b, "taskkill /PID %d /F", getpid());
    std::system(b);
}
static void searchKW(wstring kw) { DU->downloadSearchKeyword(kw); }
static void dEvent(wstring event_id) { DU->downloadEvent(event_id); }
static void verifyTag(wstring TagName, bool reverse) {
  DU->verifyTag(TagName, reverse);
}
static void uploadWork(vector<chaiscript::Boxed_Value> paths,
                       vector<chaiscript::Boxed_Value> tags, wstring content) {
  vector<struct Core::UploadImageInfo> Infos;
  vector<wstring> Tags;
  for (chaiscript::Boxed_Value bv : tags) {
    Tags.push_back(chaiscript::boxed_cast<wstring>(bv));
  }
  for (chaiscript::Boxed_Value bv : paths) {
    wstring path = chaiscript::boxed_cast<wstring>(bv);
    std::vector<char> vec;
    ifstream file(path);
    file.seekg(0, std::ios_base::end);
    std::streampos fileSize = file.tellg();
    vec.resize(fileSize);
    file.seekg(0, std::ios_base::beg);
    file.read(&vec[0], fileSize);
    auto j = DU->core.qiniu_upload(DU->core.item_postUploadToken(), vec);
    struct Core::UploadImageInfo UII;
    UII.URL = j[L"key"].as_string();
    UII.Height = j[L"h"].as_double();
    UII.Width = j[L"w"].as_double();
    UII.Ratio = 0.0;
    Infos.push_back(UII);
  }
  web::json::value req = DU->core.prepareNoteUploadArg(Tags, Infos, content);
  DU->core.item_doNewPost(Core::NewPostType::NotePost, req);
}
static void block(wstring OPType, wstring arg) {
  boost::to_upper(OPType);
  if (OPType == L"UID") {
    BOOST_LOG_TRIVIAL(debug) << "Blocking UID:" << arg << endl;
    DU->filter->UIDList.push_back(arg);
    DU->cleanUID(arg);
  } else if (OPType == L"TAG") {
    BOOST_LOG_TRIVIAL(debug) << "Blocking Tag:" << arg << endl;
    DU->filter->TagList.push_back(arg);
  } else if (OPType == L"USERNAME") {
    BOOST_LOG_TRIVIAL(debug) << "Blocking UserName:" << arg << endl;
    DU->filter->UserNameList.push_back(arg);
  } else if (OPType == L"ITEM") {
    BOOST_LOG_TRIVIAL(debug) << "Blocking item_id:" << arg << endl;
    DU->filter->ItemList.push_back(arg);
    DU->cleanItem(arg);
  } else {
    cout << "Unrecognized OPType" << endl;
  }
}
void tags() {
  mt19937 mt_rand(time(0));
  vector<wstring> Tags;
  for (web::json::value j : config[L"Tags"].as_array()) {
    Tags.push_back(j.as_string());
  }
  shuffle(Tags.begin(), Tags.end(), mt_rand);
  for (wstring item : Tags) {
    try {
      DU->downloadTag(item);
    } catch (std::exception &exp) {
      wcout << "Exception: " << exp.what()
           << " During Downloading of Tag:" << item << endl;
    }
  }
}
void searches() {
  mt19937 mt_rand(time(0));
  vector<wstring> Searches;
  for (web::json::value j : config[L"Searches"].as_array()) {
    Searches.push_back(j.as_string());
  }
  shuffle(Searches.begin(), Searches.end(), mt_rand);
  for (wstring item : Searches) {
    try {
      DU->downloadSearchKeyword(item);
    } catch (std::exception &exp) {
      wcout << "Exception: " << exp.what()
           << " During Searching Keyword:" << item << endl;
    }
  }
}
void events() {
  mt19937 mt_rand(time(0));
  vector<wstring> Events;
  for (web::json::value j : config[L"Events"].as_array()) {
    Events.push_back(j.as_string());
  }
  shuffle(Events.begin(), Events.end(), mt_rand);
  for (wstring item : Events) {
    try {
      DU->downloadEvent(item);
    } catch (std::exception &exp) {
      wcout << "Exception: " << exp.what()
           << " During Downloading Event:" << item << endl;
    }
  }
}
void works() {
  mt19937 mt_rand(time(0));
  vector<wstring> Works;
  for (web::json::value j : config[L"Works"].as_array()) {
    Works.push_back(j.as_string());
  }
  shuffle(Works.begin(), Works.end(), mt_rand);
  for (wstring item : Works) {
    try {
      DU->downloadWorkID(item);
    } catch (std::exception &exp) {
      wcout << "Exception: " << exp.what()
           << " During Downloading WorkID:" << item << endl;
    }
  }
}
void groups() {
  vector<wstring> Groups;
  mt19937 mt_rand(time(0));
  for (web::json::value j : config[L"Groups"].as_array()) {
    Groups.push_back(j.as_string());
  }
  shuffle(Groups.begin(), Groups.end(), mt_rand);
  for (wstring item : Groups) {
    try {
      DU->downloadGroupID(item);
    } catch (std::exception &exp) {
      wcout << "Exception: " << exp.what()
           << " During Downloading GroupID:" << item << endl;
    }
  }
}
void follows() {
  mt19937 mt_rand(time(0));
  vector<wstring> Follows;
  for (web::json::value j : config[L"Follows"].as_array()) {
    Follows.push_back(j.as_string());
  }
  shuffle(Follows.begin(), Follows.end(), mt_rand);
  for (wstring item : Follows) {
    try {
      DU->downloadUserLiked(item);
    } catch (std::exception &exp) {
      wcout << "Exception: " << exp.what()
           << " During Downloading User Liked:" << item << endl;
    }
  }
}
void users() {
  mt19937 mt_rand(time(0));
  vector<wstring> Users;
  for (web::json::value j : config[L"Users"].as_array()) {
    Users.push_back(j.as_string());
  }
  shuffle(Users.begin(), Users.end(), mt_rand);
  for (wstring item : Users) {
    try {
      DU->downloadUser(item);
    } catch (std::exception &exp) {
      wcout << "Exception: " << exp.what() << " During Downloading User:" << item
           << endl;
    }
  }
}
void JSONMode() {
  if (DU == nullptr) {
    cout << "You havn't initialize the downloader yet" << endl;
    return;
  }
  liked(L"");
  users();
  tags();
  searches();
  works();
  events();
  groups();
  follows();
  DU->downloadTimeline();
  if (config.has_field(L"Verify")) {
    bool ver = config[L"Verify"].as_bool();
    if (ver) {
      DU->verify();
    }
  }
  DU->join();
  delete DU;
  exit(0);
}
void Interactive() {
  cout << "Entering Interactive Mode..." << endl;
  chaiscript::ChaiScript engine;
  engine.add(chaiscript::fun(&block), "block");
  engine.add(chaiscript::fun(&verifyTag), "verifyTag");
  engine.add(chaiscript::fun(&init), "init");
  engine.add(chaiscript::fun(&work), "work");
  engine.add(chaiscript::fun(&item), "item");
  engine.add(chaiscript::fun(&forcequit), "forcequit");
  engine.add(chaiscript::fun(&verify), "verify");
  engine.add(chaiscript::fun(&verifyUID), "verifyUID");
  engine.add(chaiscript::fun(&login), "login");
  engine.add(chaiscript::fun(&group), "group");
  engine.add(chaiscript::fun(&aria2), "aria2");
  engine.add(chaiscript::fun(&unlike), "unlike");
  engine.add(chaiscript::fun(&liked), "liked");
  engine.add(chaiscript::fun(&addtype), "addtype");
  engine.add(chaiscript::fun(&user), "user");
  engine.add(chaiscript::fun(&proxy), "proxy");
  engine.add(chaiscript::fun(&quit), "quit");
  engine.add(chaiscript::fun(&tag), "tag");
  engine.add(chaiscript::fun(&searchKW), "search");
  engine.add(chaiscript::fun(&joinHandle), "join");
  engine.add(chaiscript::fun(&cleanupHandle), "cleanup");
  engine.add(chaiscript::fun(&downloadVideo), "downloadVideo");
  engine.add(chaiscript::fun(&dEvent), "event");
  engine.add(chaiscript::fun(&events), "events");
  engine.add(chaiscript::fun(&tags), "tags");
  engine.add(chaiscript::fun(&searches), "searches");
  engine.add(chaiscript::fun(&works), "works");
  engine.add(chaiscript::fun(&groups), "groups");
  engine.add(chaiscript::fun(&follows), "follows");
  engine.add(chaiscript::fun(&users), "users");
  engine.add(chaiscript::fun(&hotTags), "hotTags");
  engine.add(chaiscript::fun(&hotWorks), "hotWorks");
  engine.add(chaiscript::fun(&loginWithSKey), "loginWithSKey");
  engine.add(chaiscript::fun(&uploadWork), "uploadWork");
  engine.add(chaiscript::fun(&toggleFilter), "toggleFilter");
  wstring command;
  while (1) {
    wcout << Prefix << ":$";
    getline(wcin, command);
    if (!cin.good()) {
      break;
    }
#ifndef DEBUG
    try {
#endif
      engine.eval(toWstring.to_bytes(command));
#ifndef DEBUG
    } catch (const std::exception &exc) {
      cout << "Exception:" << exc.what() << endl;
    }
#endif
  }
}
std::wistream &operator>>(std::wistream &in,
                         logging::trivial::severity_level &sl) {
  std::wstring token;
  in >> token;
  transform(token.begin(), token.end(), token.begin(), ::tolower);
  if (token == L"info") {
    sl = boost::log::trivial::info;
  } else if (token == L"trace") {
    sl = boost::log::trivial::trace;
  } else if (token == L"debug") {
    sl = boost::log::trivial::debug;
  } else if (token == L"warning") {
    sl = boost::log::trivial::warning;
  } else if (token == L"error") {
    sl = boost::log::trivial::error;
  } else if (token == L"fatal") {
    sl = boost::log::trivial::fatal;
  } else {
    in.setstate(std::ios_base::failbit);
  }
  return in;
}
std::wistream &operator>>(std::wistream &in, mode &m) {
  std::wstring token;
  in >> token;
  transform(token.begin(), token.end(), token.begin(), ::tolower);
  if (token == L"i" || token == L"interactive") {
    m = mode::Interactive;
  } else if (token == L"json") {
    m = mode::JSON;
  } else if (token == L"block") {
    m = mode::Blocker;
  } else {
    in.setstate(std::ios_base::failbit);
  }
  return in;
}

std::ostream &operator<<(std::ostream &out, const mode &m) {
  if (m == mode::Interactive) {
    out << "Interactive";
  } else if (m == mode::JSON) {
    out << "JSON";
  } else if (m == mode::Blocker) {
    out << "Blocker";
  }
  return out;
}
int main(int argc, char **argv) {
  logging::add_common_attributes();
  po::options_description desc("BCYDownloader Options");
  desc.add_options()("help", "Print Usage")(
      "config",
      po::value<string>()->default_value(toWstring.to_bytes(BCY::expand_user(L"~/BCY.json"))),
      "Initialize Downloader using JSON at provided path")(
      "mode", po::value<int>()->default_value((int)mode::Interactive),
      "Execution Mode")(
      "log-level",
      po::value<logging::trivial::severity_level>()->default_value(
          logging::trivial::info),
      "Log Level")("HTTPProxy", po::value<string>(),
                   "HTTP Proxy")("Circles", po::value<string>(),
                                 "CircleIDs to Download, Seperated by comma")(
      "DownloadCount", po::value<int>()->default_value(16),
      "Download Thread Count")(
      "QueryCount", po::value<int>()->default_value(16), "Query Thread Count")(
      "Filters", po::value<string>(), "Type Filters Seperated by comma")(
      "Follows", po::value<string>(),
      "Download Liked Works of these UIDs, Seperated by comma")(
      "Groups", po::value<string>(),
      "Download Groups By GID, Seperated by comma")(
      "SaveBase", po::value<string>(),
      "SaveBase Directory Path")("Searches", po::value<string>(),
                                 "Keywords to Search, Seperated by comma")(
      "Tags", po::value<string>(), "Keywords to Download, Seperated by comma")(
      "UseCache", "Enable usage of cached WorkInfo")(
      "Users", po::value<string>(),
      "Download Works From these UIDs, Seperated by comma")(
      "Verify", "Verify everything in the database is downloaded")(
      "Works", po::value<string>(),
      "Download Liked Works of these WorkIDs, Seperated by comma")(
      "Events", po::value<string>(),
      "Download Works From Those EventIDs, Seperated by comma")(
      "aria2", po::value<string>(),
      "Aria2 RPC Server. Format: RPCURL[@RPCTOKEN]")(
      "email", po::value<string>(), "BCY Account email")(
      "password", po::value<string>(), "BCY Account password")(
      "DBPath", po::value<string>()->default_value(""), "BCY Database Path")(
      "UID", po::value<string>()->default_value(""), "BCY UID")(
      "sessionKey", po::value<string>()->default_value(""), "BCY sessionKey")(
      "paths", po::value<vector<string>>(), "paths to blocker");
  pos.add("paths", -1);

  try {
    po::store(po::command_line_parser(argc, argv)
                  .options(desc)
                  .positional(pos)
                  .allow_unregistered()
                  .run(),
              vm);
    po::notify(vm);
  } catch (std::exception &exp) {
    cout << "Parsing Option Error:" << exp.what() << endl;
    cout << desc << endl;
    return -1;
  }
  auto fmtTimeStamp = expr::format_date_time<posix_time::ptime>(
      "TimeStamp", "%Y-%m-%d %H:%M:%S");
  auto fmtSeverity = expr::attr<logging::trivial::severity_level>("Severity");
  log::formatter logFmt = logging::expressions::format("[%1%] [%2%] %3%") %
                          fmtTimeStamp % fmtSeverity % expr::smessage;
  auto consoleSink = log::add_console_log(std::clog);
  consoleSink->set_formatter(logFmt);
  logging::core::get()->set_filter(
      logging::trivial::severity >=
      vm["log-level"].as<logging::trivial::severity_level>());

  if (vm.count("help")) {
    cout << desc << endl;
    return 0;
  }
  if (vm["config"].as<string>() != "") {
    string JSONPath = vm["config"].as<string>();
    ifstream JSONStream(JSONPath);
    if (JSONStream.good()) {
      wstring JSONStr;
      JSONStream.seekg(0, ios::end);
      JSONStr.reserve(JSONStream.tellg());
      JSONStream.seekg(0, ios::beg);
      JSONStr.assign((istreambuf_iterator<char>(JSONStream)),
                     istreambuf_iterator<char>());
      web::json::value conf = web::json::value::parse(JSONStr);
      // Implement Options Handling.
      // Note Options inside the JSON have higher priority
      if (!conf.has_field(L"DownloadCount")) {
        config[L"DownloadCount"] = vm["DownloadCount"].as<int>();
      } else {
        config[L"DownloadCount"] = conf[L"DownloadCount"].as_integer();
      }
      if (!conf.has_field(L"DBPath")) {
        config[L"DBPath"] = web::json::value(toWstring.from_bytes(vm["DBPath"].as<string>()));
      } else {
        config[L"DBPath"] = conf[L"DBPath"];
      }
      if (!conf.has_field(L"QueryCount")) {
        config[L"QueryCount"] = vm["QueryCount"].as<int>();
      } else {
        config[L"QueryCount"] = conf[L"QueryCount"].as_integer();
      }
      if (conf.has_field(L"HTTPProxy")) {
        config[L"HTTPProxy"] = conf[L"HTTPProxy"];
      } else if (vm.count("HTTPProxy") && vm["HTTPProxy"].as<string>() != "") {
        config[L"HTTPProxy"] = web::json::value(toWstring.from_bytes(vm["HTTPProxy"].as<string>()));
      }
      for (wstring K : {L"Circles", L"Filters", L"Follows", L"Groups", L"Searches",
                       L"Tags", L"Users", L"Works", L"Events"}) {
        set<wstring> items;
        if (conf.has_field(K)) {
          for (auto item : conf[K].as_array()) {
            items.insert(item.as_string());
          }
        }
        if (vm.count(toWstring.to_bytes(K))) {
          wstring arg = vm[toWstring.to_bytes(K)].as<wstring>();
          vector<wstring> results;
          boost::split(results, arg, [](wchar_t c) { return c == L','; });
          for (wstring foo : results) {
            items.insert(foo);
          }
        }
        vector<web::json::value> j;
        for (wstring item : items) {
          j.push_back(web::json::value(item));
        }
        config[K] = web::json::value::array(j);
      }
      for (wstring K : {L"Verify", L"UseCache", L"DownloadVideo"}) {
        if (conf.has_field(K) == false) {
          if (vm.count(toWstring.to_bytes(K))) {
            config[K] = web::json::value::boolean(true);
          } else {
            config[K] = web::json::value::boolean(false);
          }
        } else {
          config[K] = conf[K];
        }
      }
      for (wstring K : {L"email", L"password", L"SaveBase"}) {
        if (vm.count(toWstring.to_bytes(K))) {
          config[K] = web::json::value(vm[toWstring.to_bytes(K)].as<wstring>());
        } else if (conf.has_field(K)) {
          config[K] = conf[K];
        }
      }

      if (conf.has_field(L"aria2") == false) {
        if (vm.count("aria2")) {
          wstring arg = vm["aria2"].as<wstring>();
          config[L"aria2"] = web::json::value();
          if (arg.find_first_of(L"@") == string::npos) {
            config[L"aria2"][L"RPCServer"] = web::json::value(arg);
          } else {
            size_t pos = arg.find_first_of(L"@");
            config[L"aria2"][L"RPCServer"] = web::json::value(arg.substr(0, pos));
            config[L"aria2"][L"secret"] = web::json::value(arg.substr(pos));
          }
        }
      } else {
        config[L"aria2"] = conf[L"aria2"];
      }
      int queryThreadCount = config[L"QueryCount"].as_integer();
      int downloadThreadCount = config[L"DownloadCount"].as_integer();
      if (conf.has_field(L"SaveBase") &&
          conf.at(L"SaveBase").is_null() == false) {
        config[L"SaveBase"] = conf[L"SaveBase"];
      } else {
        if (vm.count("SaveBase")) {
          config[L"SaveBase"] = web::json::value(vm["SaveBase"].as<wstring>());
        } else {
          cout << "SaveBase Not Specified. Default to ~/BCY/" << endl;
          config[L"SaveBase"] = web::json::value(BCY::expand_user(L"~/BCY/"));
        }
      }

      if (!config.has_field(L"DBPath")) {
        bfs::path dir(config[L"SaveBase"].as_string());
        bfs::path file("BCYInfo.db");
        bfs::path full_path = dir / file;
        config[L"DBPath"] = web::json::value(full_path.wstring());
      }
      try {

        init(config[L"SaveBase"].as_string(), queryThreadCount,
             downloadThreadCount, config[L"DBPath"].as_string());
        wcout << "Initialized Downloader at: " << config[L"SaveBase"].as_string()
             << endl;
      } catch (const SQLite::Exception &ex) {
        cout << "Database Error:" << ex.getErrorStr() << endl
             << "Error Code:" << ex.getErrorCode() << endl
             << "Extended Error Code:" << ex.getExtendedErrorCode() << endl;
        abort();
      }
      signal(SIGINT, cleanup);
      atexit(cleanup2);

      if (config.has_field(L"UseCache") &&
          config.at(L"UseCache").is_null() == false) {
        DU->useCachedInfo = config[L"UseCache"].as_bool();
      }
      if (config.has_field(L"HTTPProxy") &&
          config.at(L"HTTPProxy").is_null() == false) {
        DU->core.proxy = config[L"HTTPProxy"].as_string();
      }
      if (config.has_field(L"Types") && config.at(L"Types").is_null() == false) {
        for (auto F : config[L"Types"].as_array()) {
          DU->addTypeFilter(F.as_string());
        }
      }
      if (config.has_field(L"aria2") && config.at(L"aria2").is_null() == false) {
        if (config.at(L"aria2").has_field(L"secret")) {
          DU->secret = config[L"aria2"][L"secret"].as_string();
        }
        DU->RPCServer = config[L"aria2"][L"RPCServer"].as_string();
      }
      if (config.has_field(L"DownloadVideo") &&
          config.at(L"DownloadVideo").is_null() == false) {
        bool flag = config[L"DownloadVideo"].as_bool();
        DU->downloadVideo = flag;
      }
      if (config.has_field(L"email") && config.has_field(L"password") &&
          config.at(L"email").is_null() == false &&
          config.at(L"password").is_null() == false) {
        login(config[L"email"].as_string(), config[L"password"].as_string());
      } else if (config.has_field(L"UID") && config.has_field(L"sessionKey") &&
                 config.at(L"UID").is_null() == false &&
                 config.at(L"sessionKey").is_null() == false) {
        loginWithSKey(config[L"UID"].as_string(),
                      config[L"sessionKey"].as_string());
      }
    } else {
      cout << "Failed to Open File at:" << JSONPath << "!" << endl;
    }
  }
  if (vm["mode"].as<mode>() == mode::Interactive || DU == nullptr) {
    Interactive();
  } else if (vm["mode"].as<mode>() == mode::JSON) {
    JSONMode();
  } else {
    blockerMode(vm["paths"].as<vector<wstring>>());
  }
  return 0;
}
void blockerMode(vector<wstring> paths) {
  for (wstring arg : paths) {
    auto savebase = config[L"SaveBase"].as_string();
    auto idx = arg.compare(0, savebase.length(), savebase);
    if (idx == 0) {
      arg.replace(idx, savebase.length(), L"");
      wstringstream ss(arg);
      vector<wstring> result;
      while (ss.good()) {
        wstring substr;
        getline(ss, substr, L'/');
        result.push_back(substr);
      }
      if (result.size() == 3) {
        block(L"UID", result[2]);
      } else if (result.size() == 4) {
        block(L"ITEM", result[2]);
      }
    }
  }
}

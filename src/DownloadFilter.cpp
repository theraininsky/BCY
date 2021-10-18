#include <BCY/Base64.h>
#include <BCY/DownloadFilter.hpp>
#include <BCY/Utils.hpp>
#include <algorithm>
#include <boost/log/trivial.hpp>
#include <chaiscript/chaiscript.hpp>
#include <fstream>
#include <iostream>
#include <regex>
using namespace std;
using namespace SQLite;
using namespace chaiscript;
extern std::wstring_convert<std::codecvt_utf8<wchar_t>> toWstring;
namespace BCY {
DownloadFilter::DownloadFilter(wstring Path) {
  DBPath = Path;
  Database DB(toWstring.to_bytes(DBPath), SQLite::OPEN_CREATE || OPEN_READWRITE);
  Statement Q(DB, "SELECT Value FROM PyBCY WHERE Key=\"BCYDownloadFilter\"");
  Q.executeStep();
  if (Q.hasRow()) {
    web::json::value j = web::json::value::parse(toWstring.from_bytes(Q.getColumn(0).getString()));
    loadRulesFromJSON(j);
  }
}
DownloadFilter::~DownloadFilter() {
  web::json::value j;
  vector<web::json::value> tmp;
  const std::array<wstring, 5> names = {L"UID", L"Tag", L"UserName", L"Items",
                                       L"ScriptList"};
  const std::array<vector<wstring>, 5> elements = {
      UIDList, TagList, UserNameList, ItemList, ScriptList};
  static_assert(names.size() == elements.size(), "Array Size Mismatch");
  for (decltype(names.size()) i = 0; i < names.size(); i++) {
    vector<wstring> element = elements[i];
    vector<web::json::value> tmps;
    wstring key = names[i];
    for (wstring foo : element) {
      wstring processed;
      if (key.find(L"Script") != string::npos) {
        Base64::Encode(foo, &processed);
      } else {
        processed = foo;
      }
      tmps.push_back(web::json::value(processed));
    }
    j[key] = web::json::value::array(tmps);
  }
  Database DB(toWstring.to_bytes(DBPath), OPEN_READWRITE);
  Statement Q(DB, "INSERT OR REPLACE INTO PyBCY (Key,Value) VALUES(?,?)");
  Q.bind(1, "BCYDownloadFilter");
  Q.bind(2, toWstring.to_bytes(j.serialize()));
  Q.executeStep();
}
bool DownloadFilter::shouldBlockItem(DownloadUtils::Info &Inf) {
  wstring uid = std::get<0>(Inf);
  wstring item_id = std::get<1>(Inf);
  wstring title = std::get<2>(Inf);
  vector<wstring> tags = std::get<3>(Inf);
  struct std::tm tm;
  std::wistringstream ss(std::get<4>(Inf));
  vector<web::json::value> multis;
  for(web::json::value v:std::get<6>(Inf)){
    multis.push_back(v);
  }
  web::json::value multi=web::json::value::array(multis);
  ss >> std::get_time(&tm, L"%H:%M:%S"); // or just %T in this case
  std::time_t ctime = mktime(&tm);
  wstring desc = std::get<5>(Inf);
  if (find(UIDList.begin(), UIDList.end(), uid) != UIDList.end()) {
    BOOST_LOG_TRIVIAL(debug) << item_id << " blocked by uid rules" << endl;
    return true;
  }
  if (find(ItemList.begin(), ItemList.end(), item_id) != ItemList.end()) {
    BOOST_LOG_TRIVIAL(debug) << item_id << " blocked by item_id rules" << endl;
    return true;
  }
  for (wstring tag : tags) {
    if (find(TagList.begin(), TagList.end(), tag) != TagList.end()) {
      BOOST_LOG_TRIVIAL(debug) << item_id << " blocked by tag rules" << endl;
      return true;
    }
  }
  for (BCYFilterHandler handle : filterHandlers) {
    if (handle(Inf) < 0) {
      BOOST_LOG_TRIVIAL(debug)
          << item_id << " blocked by custom filter handler" << endl;
      return true;
    }
  }
  // Scripting
  if (ScriptList.size() > 0) {
    ChaiScript chai;
    chai.add(chaiscript::const_var(uid), "uid");
    chai.add(chaiscript::const_var(item_id), "item_id");
    chai.add(chaiscript::const_var(title), "title");
    chai.add(chaiscript::const_var(tags), "tags");
    chai.add(chaiscript::const_var(ctime), "ctime");
    chai.add(chaiscript::const_var(desc), "desc");
    chai.add(chaiscript::const_var(multi.serialize()), "multi");
    auto locals=chai.get_locals();
    for (std::wstring script : ScriptList) {
      chai.set_locals(locals);//Reset Script State
      int res = chai.eval<int>(toWstring.to_bytes(script));
      if (res > 0) {
        return false;
      } else if (res < 0) {
        BOOST_LOG_TRIVIAL(debug)
            << item_id << " blocked by script: "<<chai.boxed_cast<string>(chai.get_locals()["ScriptName"])  << endl;
        return true;
      } else {
        continue;
      }
    }
  }

  return false;
}
void DownloadFilter::addFilterHandler(BCYFilterHandler handle) {
  filterHandlers.push_back(handle);
}
bool DownloadFilter::shouldBlockAbstract(web::json::value &Inf) {

  if (Inf.has_field(L"item_detail")) {
    Inf = Inf[L"item_detail"];
  }
  wstring ctime = ensure_string(Inf[L"ctime"]);
  wstring uid = ensure_string(Inf[L"uid"]);
  wstring item_id = ensure_string(Inf[L"item_id"]);
  wstring desc = ensure_string(Inf[L"plain"]);
  vector<wstring> tags;
  for (web::json::value foo : Inf[L"post_tags"].as_array()) {
    tags.push_back(foo[L"tag_name"].as_string());
  }
  // AbstractInfo's multi is incomplete
  // Use this as a placeholder.
  // Our second pass with Detail will execute those multi-based filter rules
  web::json::array multi = web::json::value::array().as_array();
  auto tup = std::tuple<std::wstring /*UID*/, std::wstring /*item_id*/,
                        std::wstring /*Title*/, vector<wstring> /*Tags*/,
                        std::wstring /*ctime*/, std::wstring /*Description*/,
                        web::json::array /*multi*/, std::wstring /*videoID*/>(
      uid, item_id, L"", tags, ctime, desc, multi, L"");
  return shouldBlockItem(tup);
}
void DownloadFilter::loadRulesFromJSON(web::json::value rules) {
  if (rules.has_field(L"UID")) {
    auto bar = rules[L"UID"].as_array();
    for (auto item : bar) {
      UIDList.push_back(item.as_string());
    }
  }
  if (rules.has_field(L"Items")) {
    auto foo = rules[L"Items"].as_array();
    for (auto item : foo) {
      ItemList.push_back(item.as_string());
    }
  }
  if (rules.has_field(L"Tag")) {
    auto foo = rules[L"Tag"].as_array();
    for (auto item : foo) {
      TagList.push_back(item.as_string());
    }
  }
  if (rules.has_field(L"UserName")) {
    auto foo = rules[L"UserName"].as_array();
    for (auto item : foo) {
      UserNameList.push_back(item.as_string());
    }
  }
  if (rules.has_field(L"ScriptList")) {
    auto foo = rules[L"ScriptList"].as_array();
    for (auto item : foo) {
      string SRC = toWstring.to_bytes(item.as_string());
      string dec;
      Base64::Decode(SRC, &dec);
      ScriptList.push_back(toWstring.from_bytes(dec));
    }
  }
}
} // namespace BCY

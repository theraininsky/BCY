#include "BCY/DownloadFilter.hpp"
#include "BCY/Base64.h"
#include "BCY/Utils.hpp"
#include <algorithm>
#include <boost/log/trivial.hpp>
#include <fstream>
#include <iostream>
#include <regex>
using namespace std;
using namespace SQLite;
namespace BCY {
DownloadFilter::DownloadFilter(string Path) {
  DBPath = Path;
  Database DB(DBPath, SQLite::OPEN_CREATE || OPEN_READWRITE);
  Statement Q(DB, "SELECT Value FROM PyBCY WHERE Key=\"BCYDownloadFilter\"");
  Q.executeStep();
  if (Q.hasRow()) {
    web::json::value j = web::json::value::parse(Q.getColumn(0).getString());
    loadRulesFromJSON(j);
  }
}
DownloadFilter::~DownloadFilter() {
  web::json::value j;
  j["UID"] = web::json::value::array(UIDList);
  j["Work"] = web::json::value::array(WorkList);
  j["Tag"] = web::json::value::array(TagList);
  j["UserName"] = web::json::value::array(UserNameList);
  j["Type"] = web::json::value::array(TypeList);
  vector<vector<web::json::value>> lsts={AbstractScriptList,DetailScriptList,TagScriptList};
  vector<string> names={"AbstractScriptList","DetailScriptList","TagScriptList"};
  for(decltype(lsts.size()) i=0;i<lsts.size();i++){
    string name=names[i];
    vector<web::json::value>& lst=lsts[i];
    vector<web::json::value> encoded;
    for (auto item : lst) {
    string src = item.as_string();
    string enc;
    Base64::Encode(src, &enc);
    encoded.push_back(web::json::value(enc));
    }
    j[name] = web::json::value::array(encoded);
  }
  Database DB(DBPath, OPEN_READWRITE);
  Statement Q(DB, "INSERT OR REPLACE INTO PyBCY (Key,Value) VALUES(?,?)");
  Q.bind(1, "BCYDownloadFilter");
  Q.bind(2, j.serialize());
  Q.executeStep();
}
bool DownloadFilter::shouldBlockDetail(web::json::value abstract) {
  string item_id = "null";
  if (abstract.has_field("item_id")) {
    item_id = ensure_string(abstract["item_id"]);
  }
  return shouldBlockDetail(abstract, item_id);
}
bool DownloadFilter::shouldBlockDetail(web::json::value abstract,
                                       string item_id) {
  for (auto bar : UserNameList) {
    string name = bar.as_string();
    smatch match;
    string foo = abstract["profile"]["uname"].as_string();
    try {
      if (regex_search(foo, match, regex(name)) && match.size() >= 1) {
        BOOST_LOG_TRIVIAL(debug)
            << item_id << " Blocked By Regex UserName Rule:" << name << endl;
        return true;
      }
    } catch (const std::regex_error &e) {
      BOOST_LOG_TRIVIAL(error)
          << "UserName Regex Filter Error:" << e.what() << endl;
    }
  }
  int state = evalScript(abstract, item_id,ScriptEvalMode::Detail);
  return state<0;
}
bool DownloadFilter::shouldBlockTags(web::json::value Inf,string tags,string item_id){
  if(tags==""){
    if (Inf.has_field("post_tags")) {
        vector<web::json::value> tagsB; // Inner Param
      for (web::json::value tagD : Inf["post_tags"].as_array()) {
        string tag = tagD["tag_name"].as_string();
          tagsB.push_back(web::json::value(tag));
      }
        web::json::value arr=web::json::value::array(tagsB);
        tags=arr.serialize();
    } else {
      tags="[]";
    }
  }
    web::json::value tagsObj=web::json::value::parse(tags);
    web::json::array tagsArr=tagsObj.as_array();
  for(auto iter = tagsArr.cbegin(); iter != tagsArr.cend(); ++iter)
  {
    string tagName=iter->as_string();
    for (web::json::value j : TagList) {
      if (j.as_string() == tagName) {

        BOOST_LOG_TRIVIAL(debug)
            << item_id << " Blocked By Tag Rule:" << tagName << endl;
        return true;
      }
    }
  }
  return evalScript(Inf,item_id,ScriptEvalMode::Tags,tags)<0;
}
int DownloadFilter::evalScript(web::json::value Info, string item_id,ScriptEvalMode sem,string tags) {
  std::vector<web::json::value> ScriptList;
  switch(sem){
    case ScriptEvalMode::Abstract:{
      ScriptList=AbstractScriptList;
      break;
    }
    case ScriptEvalMode::Detail:{
      ScriptList=DetailScriptList;
      break;
    }
    case ScriptEvalMode::Tags:{
      ScriptList=TagScriptList;
      break;
    }
    default:{
      abort();
    }
  }
  if (ScriptList.size() > 0) {
    chaiscript::ChaiScript engine;
    string val = Info.serialize();
    engine.add(chaiscript::var(val), "Infostr");
    engine.add(chaiscript::var(tags), "tagsStr");
    engine.add(chaiscript::var(sem==ScriptEvalMode::Abstract), "isAbstract");
    engine.add(chaiscript::var(item_id), "item_id");
    engine.eval<chaiscript::Boxed_Value>("var Info=from_json(Infostr)");
    engine.eval<chaiscript::Boxed_Value>("var Tags=from_json(tagsStr)");
    auto initialState =
        engine.get_locals(); // We need to reset the local vals on each loop
    for (web::json::value j : ScriptList) {

      string scpt = j.as_string();
      engine.set_locals(initialState);
      try {
        int result = engine.eval<int>(scpt);
        auto states = engine.get_locals();
        string suff = "";
        if (result != 0) {
          if (states.find("Name") != states.end()) {
            suff = ": " + chaiscript::boxed_cast<string>(states["Name"]);
          }
        }

        if (result > 0) {
          BOOST_LOG_TRIVIAL(debug)
              << item_id << " Allowed by Script" << suff << endl;
          return result;
        } else if (result < 0) {
          BOOST_LOG_TRIVIAL(debug)
              << item_id << " Blocked by Script" << suff << endl;
          return result;
        } else {
          continue;
        }
      } catch (std::exception &exp) {
        BOOST_LOG_TRIVIAL(error)
            << "During Evalution Script:" << endl
            << scpt << endl
            << "on item_id:" << item_id
            << ", Exception is thrown:" << exp.what() << endl;
      }
    }
  }
  return 0;
}
bool DownloadFilter::shouldBlockAbstract(web::json::value abstract) {
  string item_id = "null";
  if (abstract.has_field("item_id")) {
    item_id = ensure_string(abstract["item_id"]);
  }
  return shouldBlockAbstract(abstract, item_id);
}
bool DownloadFilter::shouldBlockAbstract(web::json::value abstract,
                                         string item_id) {
  if (find(UIDList.begin(), UIDList.end(), abstract["uid"]) != UIDList.end()) {
    BOOST_LOG_TRIVIAL(debug)
        << item_id << " Blocked By UID Rule:" << abstract["uid"] << endl;
    return true;
  }
  if (abstract.has_field("post_tags")) {
    for (web::json::value &j : abstract["post_tags"].as_array()) {
      string tagName = j["tag_name"].as_string();
      for (web::json::value j : TagList) {
        if (j.as_string() == tagName) {

          BOOST_LOG_TRIVIAL(debug)
              << item_id << " Blocked By Tag Rule:" << tagName << endl;
          return true;
        }
      }
    }
  }
  if (abstract.has_field("work")) {
    for (auto bar : WorkList) {
      string name = bar.as_string();
      smatch match;
      string foo = abstract["work"].as_string();
      if (regex_search(foo, match, regex(name)) && match.size() >= 1) {
        BOOST_LOG_TRIVIAL(debug)
            << item_id << " Blocked By Regex Work Rule:" << name << endl;
        return true;
      }
    }
  }
  if (find(TypeList.begin(), TypeList.end(), abstract["type"]) !=
      TypeList.end()) {
    BOOST_LOG_TRIVIAL(debug) << item_id << " Blocked Due to its type:"
                             << ensure_string(abstract["type"]) << endl;
    return true;
  }
  int state = evalScript(abstract, item_id,ScriptEvalMode::Abstract);
  return state<0;
}
void DownloadFilter::loadRulesFromJSON(web::json::value rules) {
  if (rules.has_field("UID")) {
    auto bar = rules["UID"].as_array();
    for (auto item : bar) {
      UIDList.push_back(item);
    }
  }
  if (rules.has_field("Work")) {
    auto foo = rules["Work"].as_array();
    for (auto item : foo) {
      WorkList.push_back(item);
    }
  }
  if (rules.has_field("Tag")) {
    auto foo = rules["Tag"].as_array();
    for (auto item : foo) {
      TagList.push_back(item);
    }
  }
  if (rules.has_field("UserName")) {
    auto foo = rules["UserName"].as_array();
    for (auto item : foo) {
      UserNameList.push_back(item);
    }
  }
  if (rules.has_field("AbstractScriptList")) {
    auto foo = rules["AbstractScriptList"].as_array();
    for (auto item : foo) {
      string SRC = item.as_string();
      string dec;
      Base64::Decode(SRC, &dec);
      AbstractScriptList.push_back(web::json::value(dec));
    }
  }
  if (rules.has_field("DetailScriptList")) {
    auto foo = rules["DetailScriptList"].as_array();
    for (auto item : foo) {
      string SRC = item.as_string();
      string dec;
      Base64::Decode(SRC, &dec);
      DetailScriptList.push_back(web::json::value(dec));
    }
  }
  if (rules.has_field("TagScriptList")) {
    auto foo = rules["TagScriptList"].as_array();
    for (auto item : foo) {
      string SRC = item.as_string();
      string dec;
      Base64::Decode(SRC, &dec);
      AbstractScriptList.push_back(web::json::value(dec));
    }
  }
  if (rules.has_field("Type")) {
    auto foo = rules["Type"].as_array();
    for (auto item : foo) {
      TypeList.push_back(item);
    }
  }
}
} // namespace BCY

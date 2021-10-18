#include <BCY/Base64.h>
#include <BCY/Core.hpp>
#include <BCY/Utils.hpp>
#include <boost/algorithm/string.hpp>
#include <boost/algorithm/string/regex.hpp>
#include <boost/date_time/posix_time/posix_time.hpp>
#include <boost/filesystem.hpp>
#include <boost/log/trivial.hpp>
#include <boost/regex.hpp>
#include <boost/thread.hpp>
#include <chrono>
#include <cpprest/http_client.h>
#include <cpprest/uri.h>
#include <ctime>
using namespace std;
using namespace CryptoPP;
using namespace boost;
using namespace web;               // Common features like URIs.
using namespace web::http;         // Common HTTP functionality
using namespace web::http::client; // HTTP client features
#define BCY_KEY "com_banciyuan_AI"
std::wstring_convert<std::codecvt_utf8<wchar_t>> toWstring;
static const wstring APIBase = L"https://api.bcy.net/";
static const vector<wstring> VideoCDNURLs = {
    L"https://ib.365yg.com/video/urls/v/1/toutiao/mp4"};
namespace BCY {
Core::Core() {
  wstring alp = L"ABCDEFGHIJKLMNOPQRSTUVWXYZ";
  wstring width = generateRandomString(L"1234567890", 4);
  wstring height = generateRandomString(L"1234567890", 4);
  Params = {{L"version_code", L"4.4.1"},
            {L"mix_mode", L"1"},
            {L"account_sdk_source", L"app"},
            {L"language", L"en-US"},
            {L"channel", L"App Store"},
            {L"resolution", width + L"*" + height},
            {L"aid", L"1250"},
            {L"screen_width", width},
            {L"os_api", L"18"},
            {L"ac", L"WIFI"},
            {L"os_version", L"19.0.0"},
            {L"device_platform", L"iphone"},
            {L"device_type", L"iPhone14,5"},
            {L"vid", L""},
            {L"device_id", generateRandomString(L"1234567890", 11)},
            {L"openudid", generateRandomString(alp, 40)},
            {L"idfa", generateRandomString(alp, 40)}};
  this->Headers[L"User-Agent"] =
      L"bcy 4.3.2 rv:4.3.2.6146 (iPad; iPhone OS 9.3.3; en_US) Cronet";
  this->Headers[L"Cookie"] = L"";
}
wstring Core::EncryptData(wstring Input) {
  ECB_Mode<AES>::Encryption crypto;
  crypto.SetKey((const unsigned char *)BCY_KEY, strlen(BCY_KEY));
  string output;
  StringSource(
      toWstring.to_bytes(Input), true,
      new StreamTransformationFilter(crypto, new StringSink(output),
                                     StreamTransformationFilter::PKCS_PADDING));
  return toWstring.from_bytes(output);
}
http_response Core::GET(wstring URL, web::json::value Para,
                        map<wstring, wstring> Par) {
  if (Par.empty()) {
    Par = this->Params;
  }
  if (URL.find(L"http") == string::npos) {
    URL = APIBase + URL;
  }
  http_client_config cfg;
  if (this->proxy != L"") {
    web::web_proxy proxy(this->proxy);
    cfg.set_proxy(proxy);
  }
#ifdef DEBUG
  cfg.set_validate_certificates(false);
#endif
  cfg.set_timeout(std::chrono::seconds(this->timeout));
  http_client client(URL, cfg);
  uri_builder builder;
  for (map<wstring, wstring>::iterator it = Par.begin(); it != Par.end(); it++) {
    wstring key = it->first;
    wstring val = it->second;
    builder.append_query(key, val);
  }
  wstring body = L"";
  if (Para.is_null() == false) {
    for (auto iter = Para.as_object().cbegin(); iter != Para.as_object().cend();
         ++iter) {
      if (!body.empty()) {
        body += L"&";
      }
      body += web::uri::encode_uri(iter->first) + L"=" +
              web::uri::encode_uri(iter->second.as_string());
    }
  }

  for (decltype(retry) i = 0; i < retry + 1; i++) {
    try {
      http_request req(methods::GET);
      req.set_request_uri(builder.to_uri());
      req.headers() = this->Headers;
      req.set_body(body);
      pplx::task<web::http::http_response> task = client.request(req);
      return task.get();
    } catch (...) {
    }
  }
  http_request req(methods::GET);
  req.set_request_uri(builder.to_uri());
  req.headers() = this->Headers;
  req.set_body(body);
  pplx::task<web::http::http_response> task = client.request(req);
  task.wait();
  return task.get();
}
void Core::loginWithUIDAndSessionKey(std::wstring uid, std::wstring sKey) {
  this->UID = uid;
  this->sessionKey = sKey;
}
http_response Core::POST(wstring URL, web::json::value Para, bool Auth,
                         bool Encrypt, map<wstring, wstring> Par) {
  if (Par.empty()) {
    Par = this->Params;
  }
  if (URL.find(L"http") == string::npos) {
    URL = APIBase + URL;
  }
  if (Auth == true) {
    if (Para.has_field(L"session_key") == false && sessionKey != L"") {
      Para[L"session_key"] = web::json::value(sessionKey);
    }
  }
  if (Encrypt == true) {
    Para = EncryptParam(Para);
  } else {
    Para = mixHEXParam(Para);
  }
  http_client_config cfg;
#ifdef DEBUG
  cfg.set_validate_certificates(false);
#endif
  cfg.set_timeout(std::chrono::seconds(this->timeout));
  if (this->proxy != L"") {
    web::web_proxy proxy(this->proxy);
    cfg.set_proxy(proxy);
  }
  http_client client(URL, cfg);
  uri_builder builder;
  for (map<wstring, wstring>::iterator it = Par.begin(); it != Par.end(); it++) {
    wstring key = it->first;
    wstring val = it->second;
    builder.append_query(key, val);
  }
  wstring body = L"";
  for (auto iter = Para.as_object().cbegin(); iter != Para.as_object().cend();
       ++iter) {
    if (!body.empty()) {
      body += L"&";
    }
    body += web::uri::encode_uri(iter->first) + L"=" +
            web::uri::encode_data_string(iter->second.as_string());
  }
  for (decltype(retry) i = 0; i < retry; i++) {
    try {
      http_request req(methods::POST);
      req.set_request_uri(builder.to_uri());
      req.headers() = this->Headers;
      req.set_body(body, L"application/x-www-form-urlencoded");
      pplx::task<web::http::http_response> task = client.request(req);
      return task.get();
    } catch (...) {
    }
  }
  http_request req(methods::POST);
  req.set_request_uri(builder.to_uri());
  req.headers() = this->Headers;
  req.set_body(body, L"application/x-www-form-urlencoded");
  pplx::task<web::http::http_response> task = client.request(req);
  task.wait();
  return task.get();
}
web::json::value Core::mixHEXParam(web::json::value Params) {
  if (Params.is_null()) {
    return web::json::value::null();
  }
  web::json::value j;
  for (auto iterInner = Params.as_object().cbegin();
       iterInner != Params.as_object().cend(); ++iterInner) {
    wstring K = iterInner->first;
    wstring V = iterInner->second.as_string();
    j[K] = web::json::value(bda_hexMixedString(V));
  }
  return j;
}
web::json::value Core::item_sharePost(wstring item_id) {
  web::json::value j;
  j[L"item_id"] = web::json::value(item_id);
  auto R = POST(L"api/item/sharePost", j, true, true);
  return R.extract_json(true).get();
}
web::json::value Core::circle_status(std::wstring name){
  web::json::value j;
  j[L"name"] = web::json::value(name);
  auto R = POST(L"apiv2/circle/status", j, true, true);
  return R.extract_json(true).get();
}
web::json::value Core::ParamByCRC32URL(wstring FullURL) {
  // First a 16digit random number is generated and appended as param r=
  // So for example if baseurl is https://123456.com/item/detail/a , then random
  // number is generated and appended which makes the new URL
  // https://123456.com/item/detail/a?r=8329376462157075 Then a CRC 32 is
  // calculated among the part /item/detail/a?r=8329376462157075 Which in this
  // case is c8cbae96 , then we convert the hex to decimal representation ,
  // which is 3368791702 in this case Finally we append the crc32 value to the
  // URL as param s, so in this case the final URL is
  // https://123456.com/item/detail/a?r=8329376462157075&s=3368791702
  std::vector<std::wstring> results;
  wstringstream tmp;
  split(results, FullURL, [](char c) { return c == '/'; });
  tmp << "/";
  for (decltype(results.size()) i = 3; i < results.size(); i++) {
    tmp << results[i] << "/";
  }
  wstring CRC32Candidate = tmp.str();
  CRC32Candidate = CRC32Candidate.substr(0, CRC32Candidate.length() - 1);
  wstring nonce = generateRandomString(L"1234567890", 16);
  CRC32Candidate = CRC32Candidate + L"?r=" + nonce;
  CRC32 hash;
  unsigned int crc32_hash;
  CryptoPP::CRC32().CalculateDigest((unsigned char *)&crc32_hash,
                                    (unsigned char *)CRC32Candidate.c_str(),
                                    CRC32Candidate.size());
  web::json::value j;
  j[L"r"] = web::json::value(nonce);
  j[L"s"] = web::json::value(to_wstring(crc32_hash));
  return j;
}
web::json::value Core::videoInfo(wstring video_id) {
  // A few baseURLs, presumably CDN URLs.
  // The BaseURL is conjugated with the rest of the items. We should reverse the
  // algo to choose the correct CDN URL?
  wstring BaseURL = VideoCDNURLs[0];
  web::json::value CRC = ParamByCRC32URL(BaseURL + L"/" + video_id);
  wstring nonce = CRC[L"r"].as_string();
  wstring crc = CRC[L"s"].as_string();
  map<wstring, wstring> P{{L"r", nonce}, {L"s", crc}};
  auto R = GET(BaseURL + L"/" + video_id, json::value::null(), P);
  return R.extract_json(true).get();
}
web::json::value Core::timeline_friendfeed_hasmore(wstring since) {
  web::json::value j;
  j[L"since"] = web::json::value(since);
  auto R = POST(L"apiv2/timeline/friendfeed_hasmore", j, true, true);
  return R.extract_json(true).get();
}
web::json::value Core::timeline_stream_refresh() {
  web::json::value j;
  j[L"direction"] = web::json::value("refresh");
  auto R = POST(L"apiv2/timeline/stream", j, true, true);
  return R.extract_json(true).get();
}
web::json::value Core::qiniu_upload(web::json::value token, vector<char> &data,
                                    wstring extension) {
  wstring cloud_upToken = token[L"data"][L"cloud_upToken"].as_string();
  wstring cloud_uploader = token[L"data"][L"cloud_uploader"].as_string();
  wstring cloud_prefix = token[L"data"][L"cloud_prefix"].as_string();
  posix_time::ptime pt = posix_time::second_clock::local_time();
  wstring dateStr = to_iso_wstring(pt).substr(0, 8);
  const wstring base36 = L"0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZ";
  /* log(2**64) / log(36) = 12.38 => max 13 char + '\0' */
  wchar_t buffer[14];
  unsigned int offset = sizeof(buffer);
  int value = boost::lexical_cast<int>(dateStr);
  buffer[--offset] = '\0';
  do {
    buffer[--offset] = base36[value % 36];
  } while (value /= 36);

  wstring base36Timestamp = wstring(&buffer[offset]);
  wstring fileName =
      generateRandomString(
          L"abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXZY0123456789",
          0x20) +
      extension;
  wstring path = cloud_prefix + base36Timestamp + L"/" + fileName;
  http_client_config cfg;
#ifdef DEBUG
  cfg.set_validate_certificates(false);
#endif
  cfg.set_timeout(std::chrono::seconds(this->timeout));
  if (this->proxy != L"") {
    web::web_proxy proxy(this->proxy);
    cfg.set_proxy(proxy);
  }
  map<wstring, wstring> params;
  params[L"token"] = cloud_upToken;
  params[L"key"] = path;
  params[L"fileName"] = fileName;
  vector<unsigned char> body;
  for (map<wstring, wstring>::iterator it = params.begin(); it != params.end();
       it++) {
    wstring line = L"--werghnvt54wef654rjuhgb56trtg34tweuyrgf\r\nContent-"
                  "Disposition: form-data; name=\"" +
                  it->first + L"\"\r\n\r\n" + it->second + L"\r\n";
    copy(line.begin(), line.end(), back_inserter(body));
  }
  wstring part1 = "--werghnvt54wef654rjuhgb56trtg34tweuyrgf\r\nContent-"
                 L"Disposition: form-data; name=\"file\"; filename=\"" +
                 path + L"\"\nContent-Type:image/jpeg\r\n\r\n";
  copy(part1.begin(), part1.end(), back_inserter(body));
  copy(data.begin(), data.end(), back_inserter(body));
  wstring part2 = L"\r\n--werghnvt54wef654rjuhgb56trtg34tweuyrgf--\r\n";
  copy(part2.begin(), part2.end(), back_inserter(body));

  http_request req;
  http_client client(cloud_uploader);
  req.set_method(methods::POST);
  req.headers().add(L"Content-Length", to_string(body.size()));
  req.headers().add(
      L"Content-Type",
      L"multipart/form-data; boundary=werghnvt54wef654rjuhgb56trtg34tweuyrgf");
  req.set_body(body);
  return client.request(req).get().extract_json(true).get();
} // namespace BCY
web::json::value Core::item_postUploadToken() {
  web::json::value j;
  auto R = POST(L"api/item/postUpLoadToken", j, true, true);
  return R.extract_json(true).get();
}
web::json::value Core::item_postUploadToken(wstring GID) {
  web::json::value j;
  j[L"gid"] = web::json::value(GID);
  auto R = POST(L"api/item/postUpLoadToken", j, true, true);
  return R.extract_json(true).get();
}
web::json::value Core::timeline_stream_loadmore(wstring feed_type,
                                                int first_enter,
                                                int refresh_num) {
  web::json::value j;
  j[L"direction"] = web::json::value("loadmore");
  j[L"feed_type"] = web::json::value(feed_type);
  j[L"first_enter"] = web::json::value(first_enter);
  j[L"refresh_num"] = web::json::value(refresh_num);
  auto R = POST(L"apiv2/timeline/stream", j, true, true);
  return R.extract_json(true).get();
}
web::json::value Core::EncryptParam(web::json::value Params) {
  wstring serialized = Params.serialize();
  web::json::value j;
  wstring foo;
  Base64::Encode(EncryptData(serialized), &foo);
  j[L"data"] = web::json::value(foo);
  return j;
}
wstring Core::bda_hexMixedString(wstring input) {
  wstringstream os;
  static const char key = 5;
  os << setfill(L'0') << setw(2);
  wstringstream os2;
  for (decltype(input.length()) i = 0; i < input.length(); i++) {
    char Val = input[i] ^ key;
    os2 << hex << Val;
  }
  os << os2.str();
  return BCY::bcy_string_to_hex(os.str());
}
web::json::value Core::space_me() {
  web::json::value j;
  auto R = POST(L"api/space/me", j, true, true).extract_json(true).get();
  return R;
}
web::json::value Core::loginWithEmailAndPassword(wstring email,
                                                 wstring password) {
  web::json::value j;
  j[L"email"] = web::json::value(email);
  j[L"password"] = web::json::value(password);
  web::json::value LoginResponse =
      POST(L"passport/email/login/", j, false, false).extract_json(true).get();
  if (LoginResponse[L"message"].as_string() == L"error") {
    wstring msg = LoginResponse[L"data"][L"description"].as_string();

    return LoginResponse;
  }
  sessionKey = LoginResponse[L"data"][L"session_key"].as_string();
  j = json::value::null();
  auto R = POST(L"api/token/doLogin", j, true, true);

  Headers[L"Cookie"] = R.headers()[L"Set-Cookie"];

  LoginResponse = R.extract_json(true).get();
  UID = LoginResponse[L"data"][L"uid"].as_string();
  return LoginResponse;
}
web::json::value Core::user_detail(wstring UID) {
  web::json::value j;
  j[L"uid"] = web::json::value(UID);
  auto R = POST(L"api/user/detail", j, true, true);
  return R.extract_json(true).get();
}
web::json::value Core::image_postCover(wstring item_id) {
  web::json::value j;
  j[L"id"] = web::json::value(item_id);
  j[L"type"] = web::json::value("note");
  auto R = POST(L"api/image/postCover/", j, true, true);
  return R.extract_json(true).get();
}
bool Core::user_follow(wstring uid, bool isFollow) {
  web::json::value j;
  j[L"uid"] = web::json::value(uid);
  if (isFollow) {
    j[L"type"] = web::json::value("dofollow");
  } else {
    j[L"type"] = web::json::value("unfollow");
  }
  web::json::value R =
      POST(L"api/user/follow", j, true, true).extract_json(true).get();
  return R[L"status"].as_integer() == 1;
}
web::json::value Core::item_detail(wstring item_id, bool autoFollow) {
  web::json::value j;
  j[L"item_id"] = web::json::value(item_id);
  auto R = POST(L"api/item/detail/", j, true, true);
  web::json::value r = R.extract_json(true).get();
  if (r[L"status"] == 4010 && autoFollow) {
    // Need to Follow
    wstring UID = r[L"data"][L"profile"][L"uid"].as_string();
    user_follow(UID, true);
    R = POST(L"api/item/detail/", j, true, true);
    r = R.extract_json(true).get();
    user_follow(UID, false);
  }
  return r;
}
bool Core::item_doPostLike(wstring item_id) {
  web::json::value j;
  j[L"item_id"] = web::json::value(item_id);
  auto R = POST(L"api/item/doPostLike", j, true, true);
  web::json::value r = R.extract_json(true).get();
  return r[L"status"] == 1;
}
bool Core::item_cancelPostLike(wstring item_id) {
  web::json::value j;
  j[L"item_id"] = web::json::value(item_id);
  auto R = POST(L"api/item/cancelPostLike", j, true, true);
  web::json::value r = R.extract_json(true).get();
  return r[L"status"] == 1;
}
web::json::value Core::item_postUpLoadParam() {
  web::json::value j;
  auto R = POST(L"api/item/postUpLoadParam", j, true, true);
  web::json::value r = R.extract_json(true).get();
  return r;
}
web::json::value Core::item_doNewPost(NewPostType type, web::json::value args) {
  wstring URL = L"/api/item/doNew";
  switch (type) {
  case NewPostType::GroupAnswer: {
    URL = URL + L"GroupAnswer";
    break;
  }
  case NewPostType::ArticlePost: {
    URL = URL + L"ArticlePost";
    break;
  }
  case NewPostType::NotePost: {
    URL = URL + L"NotePost";
    break;
  }
  default: {
    throw invalid_argument("Invalid NewPost Type!");
  }
  }
  web::json::value token = item_postUpLoadParam()[L"data"][L"post_token"];
  args[L"post_token"] = token;
  auto R = POST(URL, args, true, true);
  web::json::value r = R.extract_json(true).get();
  return r;
}
web::json::value
Core::prepareNoteUploadArg(vector<wstring> &tags,
                           vector<struct UploadImageInfo> &Infos,
                           wstring content, bool allowSave, bool addWatermark,
                           bool modify, bool trans, VisibilityType view) {
  web::json::value j;
  wstring vType = L"";
  switch (view) {
  case (VisibilityType::All): {
    vType = L"all";
    break;
  }
  case (VisibilityType::Login): {
    vType = L"login";
    break;
  }
  case (VisibilityType::Fans): {
    vType = L"fans";
    break;
  }
  default: {
    throw invalid_argument("Unknown VisibilityType");
    break;
  }
  }
  vector<web::json::value> Tags;
  vector<web::json::value> URLs;
  for (wstring tag : tags) {
    Tags.push_back(web::json::value(tag));
  }
  j[L"tag_names"] = web::json::value::array(Tags);
  for (struct UploadImageInfo inf : Infos) {
    web::json::value ele;
    ele[L"h"] = web::json::value(inf.Height);
    ele[L"w"] = web::json::value(inf.Width);
    ele[L"path"] = web::json::value(inf.URL);
    ele[L"ratio"] = web::json::value(inf.Ratio);
    URLs.push_back(ele);
  }
  j[L"multi"] = web::json::value::array(URLs);
  j[L"content"] = web::json::value(content);
  web::json::value x;
  web::json::value y;
  y[L"download"] = web::json::value(allowSave);
  y[L"water_mark"] = web::json::value(addWatermark);
  x[L"save"] = y;
  web::json::value z;
  z[L"no_modify"] = web::json::value(!modify);
  z[L"no_trans"] = web::json::value(!trans);
  x[L"transmit"] = z;
  x[L"view"] = web::json::value(vType);
  j[L"authority"] = x;
  return j;
}
web::json::value Core::deletePost(wstring item_id) {
  web::json::value j;
  j[L"item_id"] = web::json::value(item_id);
  auto R = POST(L"api/item/deletePost", j, true, true);
  web::json::value r = R.extract_json(true).get();
  return r;
}
web::json::value Core::tag_status(wstring TagName) {
  web::json::value j;
  j[L"name"] = web::json::value(TagName);
  auto R = POST(L"api/tag/status", j, true, true);
  web::json::value r = R.extract_json(true).get();
  return r;
}
web::json::value Core::core_status(wstring WorkID) {
  web::json::value j;
  j[L"wid"] = web::json::value(WorkID);
  auto R = POST(L"api/core/status", j, true, true);
  web::json::value r = R.extract_json(true).get();
  return r;
}
web::json::value Core::user_userTagList() {
  web::json::value j;
  auto R = POST(L"api/user/userTagList", j, true, true);
  web::json::value r = R.extract_json(true).get();
  return r;
}
web::json::value Core::user_getUserTag(wstring uid) {
  web::json::value j;
  j[L"uid"] = web::json::value(uid);
  auto R = POST(L"api/user/getUserTag", j, true, true);
  web::json::value r = R.extract_json(true).get();
  return r;
}
web::json::value Core::circle_filterlist(wstring circle_id,
                                         CircleType circle_type,
                                         wstring circle_name) {
  web::json::value j;
  j[L"circle_id"] = web::json::value(circle_id);
  switch (circle_type) {
  case CircleType::Tag: {
    j[L"circle_type"] = web::json::value("tag");
    break;
  }
  case CircleType::Work: {
    j[L"circle_type"] = web::json::value("work");
    break;
  }
  default: {
    throw invalid_argument("Invalid Circle Type!");
  }
  }
  j[L"circle_name"] = web::json::value(circle_name);
  auto R = POST(L"apiv2/circle/filterlist/", j, true, true);
  web::json::value r = R.extract_json(true).get();
  return r;
}
vector<web::json::value>
Core::search_item_bytag(list<wstring> TagNames, PType ptype,
                        BCYListIteratorCallback callback) {
  vector<web::json::value> ret;
  int p = 0;
  web::json::value j;
  if (ptype != PType::Undef) {
    j[L"ptype"] = web::json::value(static_cast<uint32_t>(ptype));
  }
  vector<web::json::value> tmp;
  for (wstring str : TagNames) {
    tmp.push_back(web::json::value(str));
  }
  j[L"tag_list"] = web::json::value::array(tmp);
  while (true) {
    j[L"p"] = p;
    p++;
    auto R = POST(L"apiv2/search/item/bytag/", j, true, true);
    web::json::value foo = R.extract_json(true).get();
    web::json::value data = foo[L"data"][L"ItemList"];
    if (data.size() == 0) {
      return ret;
    }
    for (web::json::value &ele : data.as_array()) {
      if (callback) {
        if (!callback(ele)) {
          return ret;
        }
      }
      ret.push_back(ele);
    }
  }
  return ret;
}
web::json::value Core::group_detail(wstring GID) {
  web::json::value j;
  j[L"gid"] = web::json::value(GID);
  auto R = POST(L"api/group/detail/", j, true, true);
  web::json::value r = R.extract_json(true).get();
  return r;
}
vector<web::json::value>
Core::circle_itemhotworks(wstring circle_id, BCYListIteratorCallback callback) {
  vector<web::json::value> ret;
  int since = 0;
  web::json::value j;
  j[L"grid_type"] = web::json::value("timeline");
  j[L"id"] = web::json::value(circle_id);
  while (true) {
    if (since == 0) {
      j[L"since"] = web::json::value(since);
    } else {
      j[L"since"] = web::json::value(L"rec:" + to_wstring(since));
    }
    since++;
    auto R = POST(L"apiv2/circle/itemhotworks/", j, true, true);
    web::json::value foo = R.extract_json(true).get();
    web::json::value data = foo[L"data"][L"data"];
    if (data.size() == 0) {
      return ret;
    }
    for (web::json::value &ele : data.as_array()) {
      if (callback) {
        if (!callback(ele)) {
          return ret;
        }
      }
      ret.push_back(ele);
    }
  }
}
vector<web::json::value>
Core::circle_itemrecentworks(uint64_t circle_id, wstring name,
                             BCYListIteratorCallback callback) {
  vector<web::json::value> ret;
  wstring since = L"0";
  web::json::value j;
  j[L"grid_type"] = web::json::value("timeline");
  j[L"id"] = web::json::value(circle_id);
  j[L"name"] = web::json::value(name);
  // j[L"filter"] = web::json::value("all");
  while (true) {
    j[L"since"] = web::json::value(since);
    auto R = POST(L"apiv2/circle/item/recent/works", j, true, true);
    web::json::value foo = R.extract_json(true).get();
    web::json::value data = foo[L"data"];
    if (data.size() == 0) {
      return ret;
    }
    for (web::json::value &ele : data.as_array()) {
      if (callback) {
        if (!callback(ele)) {
          return ret;
        }
      }
      ret.push_back(ele);
    }
    since = data[data.as_array().size() - 1][L"since"].as_string();
  }
}
vector<web::json::value>
Core::circle_itemrecenttags(wstring name, wstring filter,
                            BCYListIteratorCallback callback) {
  vector<web::json::value> ret;
  wstring since = L"0";
  web::json::value j;
  j[L"grid_type"] = web::json::value("timeline");
  j[L"id"] = tag_status(name)[L"data"][L"tag_id"];
  j[L"name"] = web::json::value(name);
  j[L"filter"] = web::json::value("all");
  while (true) {
    j[L"since"] = web::json::value(since);
    auto R = POST(L"apiv2/circle/item/recent/tags", j, true, true);
    web::json::value foo = R.extract_json(true).get();
    web::json::value data = foo[L"data"];
    if (data.size() == 0) {
      return ret;
    }
    for (web::json::value &ele : data.as_array()) {
      if (callback) {
        if (!callback(ele)) {
          return ret;
        }
      }
      ret.push_back(ele);
    }
    since = data[data.as_array().size() - 1][L"since"].as_string();
  }
}
vector<web::json::value>
Core::circle_itemhottags(wstring name, BCYListIteratorCallback callback) {
  vector<web::json::value> ret;
  uint64_t since = 0;
  web::json::value j;
  j[L"grid_type"] = web::json::value("timeline");
  j[L"id"] = tag_status(name)[L"data"][L"tag_id"];
  j[L"name"] = web::json::value(name);
  // j[L"filter"] = web::json::value("all");
  while (true) {
    if (since == 0) {
      j[L"since"] = web::json::value("");
    } else {
      j[L"since"] = web::json::value(L"hot:" + to_wstring(since));
    }
    since++;
    auto R = POST(L"apiv2/circle/itemhottags", j, true, true);
    web::json::value foo = R.extract_json(true).get();
    web::json::value data = foo[L"data"][L"data"];
    if (data.size() == 0) {
      return ret;
    }
    for (web::json::value &ele : data.as_array()) {
      if (callback) {
        if (!callback(ele)) {
          return ret;
        }
      }
      ret.push_back(ele);
    }
  }
}
web::json::value Core::event_detail(wstring event_id) {
  web::json::value j;
  j[L"event_id"] = web::json::value(event_id);
  auto R = POST(L"api/event/detail", j, true, true);
  web::json::value r = R.extract_json(true).get();
  return r;
}
vector<web::json::value> Core::item_getReply(wstring item_id,
                                             BCYListIteratorCallback callback) {
  vector<web::json::value> ret;
  int p = 1;
  web::json::value j;
  j[L"item_id"] = web::json::value(item_id);
  while (true) {
    j[L"p"] = p;
    p++;
    auto R = POST(L"api/item/getReply", j, true, true);
    web::json::value foo = R.extract_json(true).get();
    web::json::value data = foo[L"data"];
    if (data.size() == 0) {
      return ret;
    }
    for (web::json::value &ele : data.as_array()) {
      if (callback) {
        if (!callback(ele)) {
          return ret;
        }
      }
      ret.push_back(ele);
    }
  }
  return ret;
}
vector<web::json::value>
Core::circle_itemRecentWorks(wstring WorkID, BCYListIteratorCallback callback) {
  vector<web::json::value> ret;
  wstring since = L"";
  web::json::value j;
  j[L"grid_type"] = web::json::value("timeline");
  j[L"id"] = web::json::value(WorkID);
  while (true) {
    j[L"since"] = web::json::value(since);
    auto R = POST(L"api/circle/itemRecentWorks", j, true, true);
    web::json::value foo = R.extract_json(true).get();
    web::json::value data = foo[L"data"];
    if (data.size() == 0) {
      return ret;
    }
    since = data[data.size() - 1][L"since"].as_string();
    for (web::json::value &ele : data.as_array()) {
      if (callback) {
        if (!callback(ele)) {
          return ret;
        }
      }
      ret.push_back(ele);
    }
  }
  return ret;
}
vector<web::json::value>
Core::timeline_getUserPostTimeLine(wstring UID,
                                   BCYListIteratorCallback callback) {
  vector<web::json::value> ret;
  wstring since = L"0";
  web::json::value j;
  j[L"grid_type"] = web::json::value("timeline");
  j[L"uid"] = web::json::value(UID);
  while (true) {
    j[L"since"] = web::json::value(since);
    auto R = POST(L"api/timeline/getUserPostTimeLine/", j, true, true);
    web::json::value foo = R.extract_json(true).get();
    web::json::value data = foo[L"data"];
    if (data.size() == 0) {
      return ret;
    }
    since = data[data.size() - 1][L"since"].as_string();
    for (web::json::value &ele : data.as_array()) {
      if (callback) {
        if (!callback(ele)) {
          return ret;
        }
      }
      ret.push_back(ele);
    }
  }
  return ret;
}
vector<web::json::value>
Core::space_getUserLikeTimeLine(wstring UID, BCYListIteratorCallback callback) {
  vector<web::json::value> ret;
  wstring since = L"0";
  web::json::value j;
  j[L"grid_type"] = web::json::value("grid");
  j[L"uid"] = web::json::value(UID);
  while (true) {
    j[L"since"] = web::json::value(since);
    auto R = POST(L"api/space/getUserLikeTimeLine/", j, true, true);
    web::json::value foo = R.extract_json(true).get();
    web::json::value data = foo[L"data"];
    if (data.size() == 0) {
      return ret;
    }
    since = data[data.size() - 1][L"since"].as_string();
    for (web::json::value &ele : data.as_array()) {
      if (callback) {
        if (!callback(ele)) {
          return ret;
        }
      }
      ret.push_back(ele);
    }
  }
  return ret;
}
vector<web::json::value>
Core::event_listPosts(wstring event_id, Order ord,
                      BCYListIteratorCallback callback) {
  vector<web::json::value> ret;
  int p = 1;
  web::json::value j;
  switch (ord) {
  case Order::Hot: {
    j[L"order"] = web::json::value("hot");
    break;
  }
  case Order::Index: {
    j[L"order"] = web::json::value("index");
    break;
  }
  default: {
    throw invalid_argument("Invalid Order Type!");
  }
  }
  j[L"event_id"] = web::json::value(event_id);
  while (true) {
    j[L"p"] = p;
    p++;
    auto R = POST(L"api/event/listPosts", j, true, true);
    web::json::value foo = R.extract_json(true).get();
    web::json::value data = foo[L"data"];
    if (data.size() == 0) {
      return ret;
    }
    for (web::json::value &ele : data.as_array()) {
      if (callback) {
        if (!callback(ele)) {
          return ret;
        }
      }
      ret.push_back(ele);
    }
  }
  return ret;
}
vector<web::json::value> Core::search(wstring keyword, SearchType type,
                                      BCYListIteratorCallback callback) {
  vector<web::json::value> ret;
  int p = 1;
  wstring URL = L"api/search/search";
  switch (type) {
  case SearchType::Content: {
    URL = URL + L"Content";
    break;
  }
  case SearchType::Works: {
    URL = URL + L"Works";
    break;
  }
  case SearchType::Tags: {
    URL = URL + L"Tags";
    break;
  }
  case SearchType::User: {
    URL = URL + L"User";
    break;
  }
  default: {
    throw invalid_argument("Invalid Search Type!");
  }
  }
  web::json::value j;
  j[L"query"] = web::json::value(keyword);
  while (true) {
    j[L"p"] = web::json::value(p);
    p++;
    auto R = POST(URL, j, true, true);

    web::json::value foo = R.extract_json(true).get();
    web::json::value data = foo[L"data"][L"results"];
    if (data.size() == 0) {
      return ret;
    }
    for (web::json::value &ele : data.as_array()) {
      if (callback) {
        if (!callback(ele)) {
          return ret;
        }
      }
      ret.push_back(ele);
    }
    if (type == SearchType::Tags || type == SearchType::Works) {
      return ret;
    }
  }
  return ret;
}
vector<web::json::value>
Core::group_listPosts(wstring GID, BCYListIteratorCallback callback) {
  vector<web::json::value> ret;
  int p = 1;
  web::json::value j;
  j[L"gid"] = web::json::value(GID);
  j[L"type"] = web::json::value("ding");
  j[L"limit"] = web::json::value(50);
  while (true) {
    j[L"p"] = p;
    p++;
    auto R = POST(L"api/group/listPosts", j, true, true);
    web::json::value foo = R.extract_json(true).get();
    web::json::value data = foo[L"data"];
    if (data.size() == 0) {
      return ret;
    }
    for (web::json::value &ele : data.as_array()) {
      if (callback) {
        if (!callback(ele)) {
          return ret;
        }
      }
      ret.push_back(ele);
    }
  }
  return ret;
}
vector<web::json::value>
Core::timeline_friendfeed(BCYListIteratorCallback callback) {
  vector<web::json::value> ret;
  std::chrono::duration<float> ms =
      std::chrono::duration_cast<std::chrono::duration<float>>(
          std::chrono::system_clock::now().time_since_epoch());
  wstring since = to_wstring(ms.count());
  web::json::value j;
  j[L"grid_type"] = web::json::value("timeline");
  j[L"direction"] = web::json::value("refresh");
  bool firstTime = true;
  while (true) {
    j[L"since"] = web::json::value(since);
    auto R = POST(L"apiv2/timeline/friendfeed", j, true, true);
    j[L"direction"] = web::json::value("loadmore");
    web::json::value foo = R.extract_json(true).get();
    web::json::value data = foo[L"data"];
    if (data.size() == 0 && firstTime == false) {
      return ret;
    }
    if (firstTime == true) {
      firstTime = false;
      continue;
    }
    since = data[data.size() - 1][L"since"].as_string();
    for (web::json::value &ele : data.as_array()) {
      if (callback) {
        if (!callback(ele)) {
          return ret;
        }
      }
      ret.push_back(ele);
    }
  }
  return ret;
}
vector<web::json::value>
Core::item_favor_itemlist(BCYListIteratorCallback callback) {
  vector<web::json::value> ret;
  wstring since = L"0";
  web::json::value j;
  j[L"grid_type"] = web::json::value("grid");
  while (true) {
    j[L"since"] = web::json::value(since);
    auto R = POST(L"apiv2/item/favor/itemlist", j, true, true);
    web::json::value foo = R.extract_json(true).get();
    web::json::value data = foo[L"data"];
    if (data.size() == 0) {
      return ret;
    }
    since = data[data.size() - 1][L"since"].as_string();
    for (web::json::value &ele : data.as_array()) {
      if (callback) {
        if (!callback(ele)) {
          return ret;
        }
      }
      ret.push_back(ele);
    }
  }
  return ret;
}
} // namespace BCY

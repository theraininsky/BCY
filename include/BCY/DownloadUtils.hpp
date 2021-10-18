#define _TURN_OFF_PLATFORM_STRING
#ifndef BCY_DOWNLOADUTILS_HPP
#define BCY_DOWNLOADUTILS_HPP
#include <BCY/Core.hpp>
#include <SQLiteCpp/SQLiteCpp.h>
#define CRYPTOPP_ENABLE_NAMESPACE_WEAK 1
#include <boost/asio.hpp>
#include <boost/filesystem.hpp>
#include <cryptopp/files.h>
#include <cryptopp/hex.h>
#include <cryptopp/md5.h>
#include <set>
#include <tuple>
#include <optional>
namespace BCY {
class DownloadFilter;
class DownloadUtils {

public:
  /**
   * \typedef Info
   * Tuple describing detail of a work
   * @param item An item in the list. Corresponding structure:<uid,item_id,Title,Tags,ctime,Description,image URL list,videoID>
   */
    typedef std::tuple<std::wstring/*UID*/,std::wstring/*item_id*/,std::wstring/*Title*/,std::vector<std::wstring>/*Tags*/,std::wstring/*ctime*/,
    std::wstring/*Description*/,web::json::array/*multi*/,std::wstring/*videoID*/> Info;
  Core core;
  DownloadFilter *filter = nullptr;
  bool useCachedInfo = true;
  bool downloadVideo = false;
  bool enableFilter = true;
  std::wstring secret = L"";
  std::wstring RPCServer = L"";
  ~DownloadUtils();
  DownloadUtils(DownloadUtils &) = delete;
  DownloadUtils() = delete;
  DownloadUtils(std::wstring PathBase, int queryThreadCount = -1,
                int downloadThreadCount = -1,
                std::wstring DBPath = L""); //-1 to use hardware thread count
  /**
   * Canonicalize Raw Server Detail JSON and construct Info object
   * \param detail Raw detail json from the server
   * \return Canonicalized Info object
   */
  Info canonicalizeRawServerDetail(web::json::value detail);
  void downloadFromAbstractInfo(web::json::value& Inf, bool runFilter);
  void downloadFromAbstractInfo(web::json::value& Inf);
  void downloadFromInfo(DownloadUtils::Info Inf,bool runFilter = true);
  std::wstring loadTitle(std::wstring title,std::wstring item_id);
  void saveInfo(Info);
  std::optional<Info> loadInfo(std::wstring item_id);
  std::wstring loadOrSaveGroupName(std::wstring name, std::wstring GID);
  web::json::value loadEventInfo(std::wstring event_id);
  void insertEventInfo(web::json::value Inf);
  void cleanup();
  void join();
  web::json::value saveOrLoadUser(std::wstring uid,std::wstring uname,std::wstring intro,std::wstring avatar,bool isValueUser);
  boost::filesystem::path getUserPath(std::wstring UID);
  boost::filesystem::path getItemPath(std::wstring UID,std::wstring item_id);
  void cleanUID(std::wstring UID);
  void cleanItem(std::wstring ItemID);
  void cleanTag(std::wstring Tag);
  void verify(std::wstring condition = L"", std::vector<std::wstring> args = {},
              bool reverse = false);
  void verifyUID(std::wstring UID, bool reverse = false);
  void verifyTag(std::wstring Tag, bool reverse = false);
  void addTypeFilter(std::wstring filter);
  void downloadGroupID(std::wstring gid);
  void downloadWorkID(std::wstring item);
  void downloadUserLiked(std::wstring uid);
  void downloadLiked();
  void downloadTag(std::wstring TagName);
  void downloadEvent(std::wstring event_id);
  void downloadUser(std::wstring uid);
  void downloadSearchKeyword(std::wstring KW);
  void downloadTimeline();
  void downloadItemID(std::wstring item_id);
  void unlikeCached();
  void downloadHotTags(std::wstring TagName,unsigned int cnt=20000);
  void downloadHotWorks(std::wstring id,unsigned int cnt=20000);

private:
  std::wstring md5(std::wstring &str);
  boost::asio::thread_pool *queryThread = nullptr;
  boost::asio::thread_pool *downloadThread = nullptr;
  std::mutex dbLock;
  std::wstring saveRoot;
  std::wstring DBPath;
  std::set<std::wstring> typeFilters;
  std::function<bool(web::json::value)> downloadCallback =
      [&](web::json::value j) {
        this->downloadFromAbstractInfo(j);
        return true;
      };
  bool stop = false;
};
} // namespace BCY
#endif

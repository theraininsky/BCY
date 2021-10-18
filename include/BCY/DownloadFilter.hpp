#define _TURN_OFF_PLATFORM_STRING
#ifndef BCY_DOWNLOADFILTER_HPP
#define BCY_DOWNLOADFILTER_HPP
#include <SQLiteCpp/SQLiteCpp.h>
#include <cpprest/json.h>
#include <BCY/DownloadUtils.hpp>
namespace BCY {
typedef std::function<int(DownloadUtils::Info&)> BCYFilterHandler;
class DownloadFilter {
public:
  DownloadFilter() = delete;
  DownloadFilter(DownloadFilter &) = delete;
  ~DownloadFilter();
  DownloadFilter(std::wstring DBPath);
  void loadRulesFromJSON(web::json::value rules);
  void addFilterHandler(BCYFilterHandler handle);
  bool shouldBlockItem(DownloadUtils::Info&);
  bool shouldBlockAbstract(web::json::value&);
  std::vector<std::wstring> UIDList;
  std::vector<std::wstring> TagList;
  std::vector<std::wstring> UserNameList;
  std::vector<std::wstring> ItemList;
  // Each should return an integer value. > 0 for allow, =0 for
  // defer, <0 for deny.
  std::vector<std::wstring> ScriptList;
private:
  std::vector<BCYFilterHandler> filterHandlers;
  SQLite::Database *DB = nullptr;
  std::wstring DBPath;
};
} // namespace BCY
#endif

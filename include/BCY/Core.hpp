#define _TURN_OFF_PLATFORM_STRING
#ifndef BCY_CORE_HPP
#define BCY_CORE_HPP
#include <cpprest/http_msg.h>
#include <cpprest/json.h>
#include <cryptopp/aes.h>
#include <cryptopp/crc.h>
#include <cryptopp/filters.h>
#include <cryptopp/hex.h>
#include <cryptopp/modes.h>
#include <iomanip>
#include <iostream>
#include <mutex>
#include <string>
namespace BCY {
//!  Core class used for interacting with BCY API
class Core {
public:
    /** @enum SearchType
     *  @brief Type of the Search to perform
     */
    enum class SearchType {
        Content = 0, /**< Posts */
        Works,       /**< Works */
        Tags,        /**< Tags */
        User,        /**< Users */
    };
    /** @enum CircleType
     *  @brief Type of a Circle
     */
    enum class CircleType {
        Tag = 0, /**< Tag */
        Work,    /**< Work */
    };
    /** @enum Order
     *  @brief Order
     */
    enum class Order {
        Hot = 0, /**< Hottest */
        Index,   /**< Timeline */
    };
    /** @enum PType
     *  @brief First class filter types. Other tags are treated as normal tags
     */
    enum class PType {
        Image = 1,   /**< Image */
        Text  = 2,   /**< Text */
        Video = 3,   /**< Video */
        Undef = 999, /**< Undefined.*/
    };
    enum class NewPostType {
        GroupAnswer = 0,
        ArticlePost,
        NotePost,
    };
    enum class PublishType {
        Note    = 0,
        Article = 1,
        Gask    = 3,
        Ganswer = 4,
        other   = 999,
    };
    enum class VisibilityType {
        All   = 0,
        Login = 1,
        Fans  = 2,
    };
    struct UploadImageInfo {
        std::wstring URL;
        float Height;
        float Width;
        float Ratio;
    };
    /**
     * \typedef BCYListIteratorCallback
     * Callback function prototype called by list iterators.
     * Return false to leave early. Otherwise the function returns until the list is empty
     * @param item An item in the list
     */
    typedef std::function<bool(web::json::value& item)> BCYListIteratorCallback;
    Core();
    std::wstring UID      = L"";
    std::wstring proxy    = L"";
    unsigned int retry   = 3;
    unsigned int timeout = 10;
    web::json::value item_sharePost(std::wstring item_id);
    web::http::http_response POST(std::wstring URL, web::json::value Payload = web::json::value(), bool Auth = true,
                                  bool EncryptParam                         = true,
                                  std::map<std::wstring, std::wstring> Params = std::map<std::wstring, std::wstring>());
    web::http::http_response GET(std::wstring URL, web::json::value Payload = web::json::value(),
                                 std::map<std::wstring, std::wstring> Params = std::map<std::wstring, std::wstring>());
    bool item_doPostLike(std::wstring item_id);
    bool item_cancelPostLike(std::wstring item_id);
    bool user_follow(std::wstring uid, bool isFollow);
    web::json::value user_detail(std::wstring UID);
    web::json::value image_postCover(std::wstring item_id);
    web::json::value space_me();
    web::json::value prepareNoteUploadArg(std::vector<std::wstring>& tags, std::vector<struct UploadImageInfo>& Infos,
                                          std::wstring content = L"", bool allowSave = false, bool addWatermark = true,
                                          bool modify = false, bool trans = false,
                                          VisibilityType view = VisibilityType::All);
    /**
     * Login with email and password
     * \param email E-Mail address
     * \param password Password
     * \return On failure, has ``[data][description]`` which contains failure reason. On success can be ignored
     */
    web::json::value loginWithEmailAndPassword(std::wstring email, std::wstring password);
    /**
     * Login with UID and sessionKey
     * \param uid User's UID.
     * \param sessionKey Session Key. Can be dumped from a jailbroken iOS device or a rooted Android device.
     */
    void loginWithUIDAndSessionKey(std::wstring uid, std::wstring sessionKey);
    web::json::value item_detail(std::wstring item_id, bool autoFollow = true);
    web::json::value tag_status(std::wstring TagName);
    web::json::value user_getUserTag(std::wstring uid);
    web::json::value deletePost(std::wstring item_id);
    web::json::value event_detail(std::wstring event_id); // https://bcy.net/tags/event for a full event list
    /**
     * Returns a list of user-assignable tags and their ut_id
     */
    web::json::value user_userTagList(); // A list of all available user tags and their ut_id

    web::json::value circle_filterlist(std::wstring circle_id, CircleType circle_type, std::wstring circle_name);
    /**
     * List Hot items of a tag
     * \return ``{"status":1,"message":"","debug_info":"","data":{"id":"123456","name":"name","relative_wid":-1,"type":"tag","nickname":"","intro":"圈子欢迎你","cover":"https://p9-bcy.byteimg.com","admin_users":[],"follow_status":true,"follow_count":17,"affiches":[],"announces":[],"members":[{"uid":1,"uname":"X","avatar":"https://"},{"uid":1,"uname":"X","avatar":"https://"},{"uid":1,"uname":"X","avatar":"https://"}]}}``
     */
    std::vector<web::json::value> circle_itemhottags(std::wstring name,
                                                     BCYListIteratorCallback callback = BCYListIteratorCallback());
    web::json::value group_detail(std::wstring GID);
    web::json::value timeline_stream_refresh();
    web::json::value item_postUploadToken();
    web::json::value item_postUpLoadParam();
    web::json::value qiniu_upload(web::json::value token, std::vector<char>& data, std::wstring extension = L"jpg");
    web::json::value item_doNewPost(NewPostType type, web::json::value args);
    web::json::value item_postUploadToken(std::wstring GID);
    web::json::value timeline_stream_loadmore(std::wstring feed_type = L"", int first_enter = 1, int refresh_num = 25);
    web::json::value core_status(std::wstring WorkID);
    web::json::value videoInfo(std::wstring video_id);
    web::json::value timeline_friendfeed_hasmore(std::wstring since);
    std::vector<web::json::value> event_listPosts(std::wstring event_id, Order ord = Order::Hot,
                                                  BCYListIteratorCallback callback = BCYListIteratorCallback());
  /**
   * Status of a circle by its name
   * \return ``{"status":1,"message":"","debug_info":"","data":{"id":"5320","name":"name","relative_wid":-1,"type":"tag","nickname":"","intro":"圈子欢迎你","cover":"https://p9-bcy.byteimg.com","admin_users":[],"follow_status":true,"follow_count":17,"affiches":[],"announces":[],"members":[{"uid":1,"uname":"X","avatar":"https://"},{"uid":1,"uname":"X","avatar":"https://"},{"uid":1,"uname":"X","avatar":"https://"}]}}``
   */
    web::json::value circle_status(std::wstring name);
    std::vector<web::json::value> circle_itemrecenttags(std::wstring TagName, std::wstring Filter,
                                                        BCYListIteratorCallback callback = BCYListIteratorCallback());
    std::vector<web::json::value> item_getReply(std::wstring item_id,
                                                BCYListIteratorCallback callback = BCYListIteratorCallback());
    std::vector<web::json::value> search_item_bytag(std::list<std::wstring> TagNames, PType ptype = PType::Undef,
                                                    BCYListIteratorCallback callback = BCYListIteratorCallback());
    std::vector<web::json::value> circle_itemRecentWorks(std::wstring WorkID,
                                                         BCYListIteratorCallback callback = BCYListIteratorCallback());
    std::vector<web::json::value> timeline_getUserPostTimeLine(
        std::wstring UID, BCYListIteratorCallback callback = BCYListIteratorCallback());
    std::vector<web::json::value> space_getUserLikeTimeLine(
        std::wstring UID, BCYListIteratorCallback callback = BCYListIteratorCallback());
    std::vector<web::json::value> circle_itemrecentworks(uint64_t circle_id, std::wstring name,
                                                         BCYListIteratorCallback callback = BCYListIteratorCallback());
    std::vector<web::json::value> circle_itemrecenttags(std::wstring name,
                                                        BCYListIteratorCallback callback = BCYListIteratorCallback());
    std::vector<web::json::value> search(std::wstring keyword, SearchType type,
                                         BCYListIteratorCallback callback = BCYListIteratorCallback());
    std::vector<web::json::value> group_listPosts(std::wstring GID,
                                                  BCYListIteratorCallback callback = BCYListIteratorCallback());
    std::vector<web::json::value> timeline_friendfeed(BCYListIteratorCallback callback = BCYListIteratorCallback());
    std::vector<web::json::value> circle_itemhotworks(std::wstring circle_id,
                                                      BCYListIteratorCallback callback = BCYListIteratorCallback());
    std::vector<web::json::value> item_favor_itemlist(BCYListIteratorCallback callback = BCYListIteratorCallback());

private:
    std::wstring bda_hexMixedString(std::wstring input);
    std::map<std::wstring, std::wstring> Params;
    web::http::http_headers Headers;
    std::wstring sessionKey = L"";
    std::wstring EncryptData(std::wstring Input);
    web::json::value EncryptParam(web::json::value Params);
    web::json::value mixHEXParam(web::json::value Params);
    web::json::value ParamByCRC32URL(std::wstring FullURL);
};
} // namespace BCY
#endif

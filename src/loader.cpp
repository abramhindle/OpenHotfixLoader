#include <pch.h>

#include <doctest/doctest.h>

#include "args.h"
#include "loader.h"
#include "util.h"
#include "version.h"

using mod_file_identifier = std::wstring;

namespace ohl::loader {
TEST_SUITE_BEGIN("loader");

#pragma region Consts

class mod_file;

static const std::wstring WHITESPACE = L" \f\n\r\t\b";

static const std::wstring HOTFIX_COMMAND = L"spark";
static const std::wstring NEWS_COMMAND = L"injectnewsitem";
static const std::wstring EXEC_COMMAND = L"exec";
static const std::wstring URL_COMMAND = L"url=";

static const std::wstring TYPE_11_DELAY_TYPE = L"SparkEarlyLevelPatchEntry";
static const std::wstring TYPE_11_DELAY_VALUE =
    L"(1,1,0,{map}),/Game/Pickups/Ammo/"
    L"BPAmmoItem_Pistol.Default__BPAmmoItem_Pistol_C,ItemMeshComponent.Object..StaticMesh,0,,"
    L"StaticMesh'\"{mesh}\"'";
static const std::vector<std::wstring> TYPE_11_DELAY_MESHES = {
    L"/Engine/EditorMeshes/Camera/SM_CraneRig_Arm.SM_CraneRig_Arm",
    L"/Engine/EditorMeshes/Camera/SM_CraneRig_Base.SM_CraneRig_Base",
    L"/Engine/EditorMeshes/Camera/SM_CraneRig_Body.SM_CraneRig_Body",
    L"/Engine/EditorMeshes/Camera/SM_CraneRig_Mount.SM_CraneRig_Mount",
    L"/Engine/EditorMeshes/Camera/SM_RailRig_Mount.SM_RailRig_Mount",
    L"/Engine/EditorMeshes/Camera/SM_RailRig_Track.SM_RailRig_Track",
    L"/Game/Pickups/Ammo/Model/Meshes/SM_ammo_pistol.SM_ammo_pistol",
};

static const std::wstring OHL_NEWS_ITEM_IMAGE_URL = L"" OHL_GITHUB_RAW_URL "news_icon.png";

static const std::wstring OHL_NEWS_ITEM_ARTICLE_URL = L"" OHL_GITHUB_URL "releases";

#pragma endregion

// This default works relative to the cwd, we'll try replace it later.
static std::filesystem::path mod_dir = "ohl-mods";

static std::mutex known_mod_files_mutex;
static std::unordered_map<mod_file_identifier, std::shared_ptr<mod_file>> known_mod_files{};

#pragma region Types

/**
 * @brief Class holding all the data that can be extracted from a region of a mod file.
 */
class mod_data {
   public:
    std::deque<hotfix> hotfixes{};
    std::vector<hotfix> type_11_hotfixes{};
    std::unordered_set<std::wstring> type_11_maps{};
    std::deque<news_item> news_items{};

    /**
     * @brief Checks if this object holds no mod data.
     *
     * @return True if empty, false otherwise.
     */
    bool is_empty(void) const {
        return this->hotfixes.size() == 0 && this->type_11_hotfixes.size() == 0
               && this->type_11_maps.size() == 0 && this->news_items.size() == 0;
    }

    /**
     * @brief Appends the mod data from this object to the end of that of another.
     *
     * @param other The other mod data object to append to.
     */
    void append_to(mod_data& other) const {
        other.hotfixes.insert(other.hotfixes.end(), this->hotfixes.begin(), this->hotfixes.end());
        other.type_11_hotfixes.insert(other.type_11_hotfixes.end(), this->type_11_hotfixes.begin(),
                                      this->type_11_hotfixes.end());
        other.type_11_maps.insert(this->type_11_maps.begin(), this->type_11_maps.end());
        other.news_items.insert(other.news_items.end(), this->news_items.begin(),
                                this->news_items.end());
    }
};

TEST_CASE("loader::mod_data::append_to") {
    const mod_data data_a{{{L"SparkPatchEntry", L"(1,1,0,),/Some/Hotfix/Here"},
                           {L"SparkPatchEntry", L"(1,1,0,),/Another/Hotfix/"}},
                          {{L"SparkEarlyLevelPatchEntry", L"(1,11,0,SomeMap_P)"}},
                          {L"SomeMap_P"},
                          {{L"header", L"image", L"article", L"body"}}};

    const mod_data data_b{{{L"Key", L"Value"}}, {}, {}, {}};

    const mod_data data_empty{};

    const std::deque<hotfix> expected_a_append_to_b_hotfixes = {
        {L"Key", L"Value"},
        {L"SparkPatchEntry", L"(1,1,0,),/Some/Hotfix/Here"},
        {L"SparkPatchEntry", L"(1,1,0,),/Another/Hotfix/"},
    };

    const std::deque<hotfix> expected_b_append_to_a_hotfixes = {
        {L"SparkPatchEntry", L"(1,1,0,),/Some/Hotfix/Here"},
        {L"SparkPatchEntry", L"(1,1,0,),/Another/Hotfix/"},
        {L"Key", L"Value"},
    };

    SUBCASE("against empty") {
        mod_data data_a_copy = data_a;

        data_empty.append_to(data_a_copy);

        CHECK(ITERABLE_EQUAL(data_a_copy.hotfixes, data_a.hotfixes));
        CHECK(ITERABLE_EQUAL(data_a_copy.type_11_hotfixes, data_a.type_11_hotfixes));
        CHECK(ITERABLE_EQUAL(data_a_copy.type_11_maps, data_a.type_11_maps));
        CHECK(ITERABLE_EQUAL(data_a_copy.news_items, data_a.news_items));
    }

    SUBCASE("against filled") {
        mod_data data_a_copy = data_a;
        mod_data data_b_copy = data_b;

        data_a.append_to(data_b_copy);
        data_b.append_to(data_a_copy);

        CHECK(ITERABLE_EQUAL(data_a_copy.hotfixes, expected_b_append_to_a_hotfixes));

        CHECK(ITERABLE_EQUAL(data_a_copy.type_11_hotfixes, data_a.type_11_hotfixes));
        CHECK(ITERABLE_EQUAL(data_a_copy.type_11_maps, data_a.type_11_maps));
        CHECK(ITERABLE_EQUAL(data_a_copy.news_items, data_a.news_items));

        CHECK(ITERABLE_EQUAL(data_b_copy.hotfixes, expected_a_append_to_b_hotfixes));

        CHECK(ITERABLE_EQUAL(data_b_copy.type_11_hotfixes, data_a.type_11_hotfixes));
        CHECK(ITERABLE_EQUAL(data_b_copy.type_11_maps, data_a.type_11_maps));
        CHECK(ITERABLE_EQUAL(data_b_copy.news_items, data_a.news_items));
    }
}

TEST_CASE("loader::mod_data::is_empty") {
    mod_data data{};
    REQUIRE(data.is_empty() == true);

    mod_data data_hotfix{};
    data_hotfix.hotfixes.emplace_back();

    mod_data data_type_11_hotfix{};
    data_type_11_hotfix.type_11_hotfixes.emplace_back();

    mod_data data_type_11_map{};
    data_type_11_map.type_11_maps.insert(L"");

    mod_data data_news{};
    data_news.news_items.emplace_back();

    CHECK(data_hotfix.is_empty() == false);
    CHECK(data_type_11_hotfix.is_empty() == false);
    CHECK(data_type_11_map.is_empty() == false);
    CHECK(data_news.is_empty() == false);
}

/**
 * @brief Class representing mod data coming from another file.
 */
class remote_mod_data {
   public:
    const mod_file_identifier identifier;

    remote_mod_data(const mod_file_identifier& identifier) : identifier(identifier) {}
};

/**
 * @brief Base class holding a section of a mod file, and some metadata about it.
 */
class mod_file {
   protected:
    void load_from_stream(std::istream& stream, bool allow_exec);

    /**
     * @brief Adds a mod data object to this file's sections.
     *
     * @param data The mod data to add.
     */
    void push_mod_data(const mod_data& data) {
        if (!data.is_empty()) {
            this->sections.push_back(data);
        }
    }

    /**
     * @brief Adds a remote to this file's sections, as well as to the global list of known files.
     * @note If the file was not previously known, starts loading it.
     * @note Edits the global list atomically.
     *
     * @param file The file to register.
     */
    void register_remote_file(const std::shared_ptr<mod_file> file) {
        auto identifier = file->get_identifier();
        this->sections.emplace_back(identifier);

        bool is_known = true;

        {
            std::lock_guard<std::mutex> lock(known_mod_files_mutex);

            if (known_mod_files.find(identifier) == known_mod_files.end()) {
                known_mod_files[identifier] = file;
                is_known = false;
            }
        }

        if (!is_known) {
            file->load();
        }
    }

   public:
    std::vector<std::variant<mod_data, remote_mod_data>> sections;

    /**
     * @brief Appends all the mod data from this file to the end of a mod data object.
     * @note Recursively looks up remote files.
     * @note Calls join, will block until the data is ready.
     *
     * @param data The mod data object to append to.
     * @param seen_files A list of files which have already been seen. Any nested files which have
     *                   yet to be seen will be appended in the order encountered. Optional.
     */
    void append_to(mod_data& data) {
        std::vector<mod_file_identifier> seen_files;
        this->append_to(data, seen_files);
    }
    void append_to(mod_data& data, std::vector<mod_file_identifier>& seen_files) {
        this->join();

        for (const auto& section : this->sections) {
            if (std::holds_alternative<mod_data>(section)) {
                std::get<mod_data>(section).append_to(data);
            } else {
                auto file = known_mod_files.at(std::get<remote_mod_data>(section).identifier);

                auto identifier = file->get_identifier();
                if (std::find(seen_files.begin(), seen_files.end(), identifier)
                    != seen_files.end()) {
                    LOGD << "[OHL] Already seen " << identifier;
                    continue;
                } else {
                    LOGD << "[OHL] Appending mod data for " << identifier;
                }
                seen_files.push_back(identifier);

                file->append_to(data, seen_files);
            }
        }
    }

    /**
     * @brief Gets a unique identifier for this mod file.
     *
     * @return The mod's identifier.
     */
    virtual mod_file_identifier get_identifier(void) const = 0;

    /**
     * @brief Gets the display name of this mod file.
     *
     * @return The mod's display name.
     */
    virtual std::wstring get_display_name(void) const = 0;

    /**
     * @brief Attempts to load the contents of this mod file.
     * @note May be async, must call join to ensure this has finished.
     */
    virtual void load(void) = 0;

    /**
     * @brief Joins any threads started by loading this mod file.
     */
    virtual void join(void) {}
};

/**
 * @brief Class for mod file data based on a local file.
 */
class mod_file_local : public mod_file {
   public:
    const std::filesystem::path path;

    mod_file_local(const std::filesystem::path& path) : path(path) {}

    virtual mod_file_identifier get_identifier(void) const { return this->path.wstring(); }

    virtual std::wstring get_display_name(void) const {
        if (this->path.parent_path() == mod_dir) {
            return this->path.filename().wstring();
        } else {
            return this->path.wstring();
        }
    }

    TEST_CASE_CLASS("loader::mod_file_local::get_display_name") {
        auto original_mod_dir = mod_dir;
        mod_dir = "tests";

        // Use an empty subcase to make sure we restore the mod dir if this fails
        SUBCASE("") {
            CHECK(mod_file_local{mod_dir / "mod_file.bl3hotfix"}.get_display_name()
                  == L"mod_file.bl3hotfix");

            CHECK(mod_file_local{mod_dir / "nested_folder" / "mod_in_nested.txt"}.get_display_name()
                  == L"tests\\nested_folder\\mod_in_nested.txt");

            CHECK(mod_file_local{mod_dir / "mod_without_extension"}.get_display_name()
                  == L"mod_without_extension");

            CHECK(mod_file_local{mod_dir / "mod.with.multiple.dots.txt"}.get_display_name()
                  == L"mod.with.multiple.dots.txt");

            CHECK(mod_file_local{mod_dir / ".mod- !#with ()'symbols;'+ .txt"}.get_display_name()
                  == L".mod- !#with ()'symbols;'+ .txt");
        }

        mod_dir = original_mod_dir;
    }

    virtual void load(void) {
        LOGD << "[OHL] Loading " << path;

        std::ifstream stream{path};
        if (!stream.is_open()) {
            LOGE << "[OHL]: Error opening file '" << path;
            return;
        }

        this->load_from_stream(stream, true);
    }

    TEST_CASE_CLASS("loader::mod_file_local::load - load_from_stream identical") {
        mod_file_local local_file{std::filesystem::path("tests") / "basic_mod.bl3hotfix"};
        mod_file_local stream_file{"dummy"};

        std::stringstream stream{};
        stream << "SparkLevelPatchEntry,(1,1,1,Crypt_P),/Game/Cinematics/_Design/NPCs/"
                  "BPCine_Actor_Typhon.BPCine_Actor_Typhon_C:SkeletalMesh_GEN_VARIABLE,"
                  "bEnableUpdateRateOptimizations,4,True,False\n";

        const hotfix expected_hotfix{
            L"SparkLevelPatchEntry",
            L"(1,1,1,Crypt_P),/Game/Cinematics/_Design/NPCs/"
            "BPCine_Actor_Typhon.BPCine_Actor_Typhon_C:SkeletalMesh_GEN_VARIABLE,"
            "bEnableUpdateRateOptimizations,4,True,False"};

        local_file.load();
        local_file.join();
        stream_file.load_from_stream(stream, true);

        REQUIRE(local_file.sections.size() == 1);
        REQUIRE(std::holds_alternative<mod_data>(local_file.sections[0]));
        auto local_data = std::get<mod_data>(local_file.sections[0]);

        REQUIRE(stream_file.sections.size() == 1);
        REQUIRE(std::holds_alternative<mod_data>(stream_file.sections[0]));
        auto stream_data = std::get<mod_data>(stream_file.sections[0]);

        REQUIRE(local_data.hotfixes.size() == 1);
        CHECK(local_data.type_11_hotfixes.empty());
        CHECK(local_data.type_11_maps.empty());
        CHECK(local_data.news_items.empty());

        REQUIRE(stream_data.hotfixes.size() == 1);
        CHECK(stream_data.type_11_hotfixes.empty());
        CHECK(stream_data.type_11_maps.empty());
        CHECK(stream_data.news_items.empty());

        CHECK(local_data.hotfixes[0] == stream_data.hotfixes[0]);
        CHECK(local_data.hotfixes[0] == expected_hotfix);
        CHECK(stream_data.hotfixes[0] == expected_hotfix);
    }

    // Have to put this test here since it uses a protected method, and `mod_file` is abstract
    TEST_CASE_CLASS("loader::mod_file::register_remote_file") {
        known_mod_files.clear();
        REQUIRE(known_mod_files.empty());

        mod_file_local base{"base.bl3hotfix"};
        REQUIRE(base.sections.empty());

        std::shared_ptr<mod_file> remote = std::make_shared<mod_file_local>("remote.bl3hotfix");

        SUBCASE("Unknown file") {
            base.register_remote_file(remote);

            REQUIRE(base.sections.size() == 1);
            auto section = base.sections[0];

            REQUIRE(std::holds_alternative<remote_mod_data>(section));
            auto remote_data = std::get<remote_mod_data>(section);

            REQUIRE(remote_data.identifier == remote->get_identifier());

            REQUIRE(known_mod_files.count(remote_data.identifier) == 1);
            REQUIRE(known_mod_files.at(remote_data.identifier) == remote);
        }

        SUBCASE("Known file") {
            std::shared_ptr<mod_file> known_remote =
                std::make_shared<mod_file_local>("remote.bl3hotfix");
            REQUIRE(known_remote->get_identifier() == remote->get_identifier());
            known_mod_files[known_remote->get_identifier()] = known_remote;

            base.register_remote_file(remote);

            REQUIRE(base.sections.size() == 1);
            auto section = base.sections[0];

            REQUIRE(std::holds_alternative<remote_mod_data>(section));
            auto remote_data = std::get<remote_mod_data>(section);

            REQUIRE(remote_data.identifier == remote->get_identifier());

            REQUIRE(known_mod_files.count(remote_data.identifier) == 1);
            REQUIRE(known_mod_files.at(remote_data.identifier) != remote);
            REQUIRE(known_mod_files.at(remote_data.identifier) == known_remote);
        }

        known_mod_files.clear();
    }
};

TEST_CASE("loader::mod_file::append_to") {
    mod_file_local file{std::filesystem::path("tests") / "basic_mod.bl3hotfix"};
    file.load();

    REQUIRE(file.sections.size() == 1);
    REQUIRE(std::holds_alternative<mod_data>(file.sections[0]));
    auto file_data = std::get<mod_data>(file.sections[0]);

    mod_data empty_data{};
    mod_data filled_data{{{L"Key", L"Value"}}, {}, {}, {}};

    const std::vector<hotfix> expected_append_to_filled_hotfixes = {
        {L"Key", L"Value"},
        {L"SparkLevelPatchEntry",
         L"(1,1,1,Crypt_P),/Game/Cinematics/_Design/NPCs/"
         "BPCine_Actor_Typhon.BPCine_Actor_Typhon_C:SkeletalMesh_GEN_VARIABLE,"
         "bEnableUpdateRateOptimizations,4,True,False"}};

    std::vector<mod_file_identifier> seen_files{};

    file.append_to(empty_data, seen_files);
    CHECK(seen_files.empty());  // Only adds nested files

    file.append_to(filled_data, seen_files);
    CHECK(seen_files.empty());
    CHECK(ITERABLE_EQUAL(empty_data.hotfixes, file_data.hotfixes));
    CHECK(ITERABLE_EQUAL(empty_data.type_11_hotfixes, file_data.type_11_hotfixes));
    CHECK(ITERABLE_EQUAL(empty_data.type_11_maps, file_data.type_11_maps));
    CHECK(ITERABLE_EQUAL(empty_data.news_items, file_data.news_items));

    CHECK(ITERABLE_EQUAL(filled_data.hotfixes, expected_append_to_filled_hotfixes));

    CHECK(ITERABLE_EQUAL(filled_data.type_11_hotfixes, file_data.type_11_hotfixes));
    CHECK(ITERABLE_EQUAL(filled_data.type_11_maps, file_data.type_11_maps));
    CHECK(ITERABLE_EQUAL(filled_data.news_items, file_data.news_items));
}

/**
 * @brief Class for mod file data based on a url.
 */
class mod_file_url : public mod_file {
   private:
    std::future<void> download;

   public:
    const std::wstring url;

    mod_file_url(const std::wstring& url) : url(url) {}

    virtual mod_file_identifier get_identifier(void) const { return this->url; }

    virtual std::wstring get_display_name(void) const {
        if (this->url.find_last_of('/') == std::string::npos) {
            return this->url;
        } else {
            auto unescaped = ohl::util::unescape_url(this->url, false);
            auto name_start_pos = unescaped.find_last_of('/') + 1;
            auto name_end_pos = unescaped.find_first_of(L"#?", name_start_pos);

            return unescaped.substr(name_start_pos, name_end_pos - name_start_pos) + L" (url)";
        }
    }

    TEST_CASE_CLASS("loader::mod_file_url::get_display_name") {
        CHECK(mod_file_url{L"https://example.com/mod.bl3hotfix"}.get_display_name()
              == L"mod.bl3hotfix (url)");

        CHECK(mod_file_url{L"https://example.com/nested/nested/mod.bl3hotfix"}.get_display_name()
              == L"mod.bl3hotfix (url)");

        CHECK(mod_file_url{L"https://exa%6Dple.com/%6Dy%20mod.bl3hotfix"}.get_display_name()
              == L"my mod.bl3hotfix (url)");

        CHECK(mod_file_url{L"https://example.com/mod.bl3hotfix?query=abc"}.get_display_name()
              == L"mod.bl3hotfix (url)");

        CHECK(mod_file_url{L"https://example.com/mod.bl3hotfix#anchor"}.get_display_name()
              == L"mod.bl3hotfix (url)");
    }

    virtual void load(void) {
        LOGD << "[OHL] Loading " << this->url;

        this->download = cpr::GetCallback(
            [&](const cpr::Response& resp) {
                LOGD << "[OHL] Finished downloading " << this->url;

                if (resp.status_code == 0) {
                    LOGE << "[OHL] Error downloading '" << this->url << "': " << resp.error.message;
                    return;
                } else if (resp.status_code >= 400) {
                    LOGE << "[OHL] Error downloading '" << this->url << "': " << resp.status_code;
                    return;
                }

                std::stringstream stream{resp.text};

                this->load_from_stream(stream, false);
            },
            cpr::Url{ohl::util::narrow(this->url)},
            // An empty string tells libcurl to accept whatever encodings it can
            cpr::AcceptEncoding{{""}});
    };

    TEST_CASE_CLASS("loader::mod_file_url::load - load_from_stream identical") {
        mod_file_url url_file{L"" OHL_GITHUB_RAW_URL "tests/basic_mod.bl3hotfix"};
        mod_file_url stream_file{L"dummy"};

        std::stringstream stream{};
        stream << "SparkLevelPatchEntry,(1,1,1,Crypt_P),/Game/Cinematics/_Design/NPCs/"
                  "BPCine_Actor_Typhon.BPCine_Actor_Typhon_C:SkeletalMesh_GEN_VARIABLE,"
                  "bEnableUpdateRateOptimizations,4,True,False\n";

        const hotfix expected_hotfix{
            L"SparkLevelPatchEntry",
            L"(1,1,1,Crypt_P),/Game/Cinematics/_Design/NPCs/"
            "BPCine_Actor_Typhon.BPCine_Actor_Typhon_C:SkeletalMesh_GEN_VARIABLE,"
            "bEnableUpdateRateOptimizations,4,True,False"};

        url_file.load();
        url_file.join();
        stream_file.load_from_stream(stream, true);

        REQUIRE(url_file.sections.size() == 1);
        REQUIRE(std::holds_alternative<mod_data>(url_file.sections[0]));
        auto url_data = std::get<mod_data>(url_file.sections[0]);

        REQUIRE(stream_file.sections.size() == 1);
        REQUIRE(std::holds_alternative<mod_data>(stream_file.sections[0]));
        auto stream_data = std::get<mod_data>(stream_file.sections[0]);

        REQUIRE(url_data.hotfixes.size() == 1);
        CHECK(url_data.type_11_hotfixes.empty());
        CHECK(url_data.type_11_maps.empty());
        CHECK(url_data.news_items.empty());

        REQUIRE(stream_data.hotfixes.size() == 1);
        CHECK(stream_data.type_11_hotfixes.empty());
        CHECK(stream_data.type_11_maps.empty());
        CHECK(stream_data.news_items.empty());

        CHECK(url_data.hotfixes[0] == stream_data.hotfixes[0]);
        CHECK(url_data.hotfixes[0] == expected_hotfix);
        CHECK(stream_data.hotfixes[0] == expected_hotfix);
    }

    virtual void join(void) {
        if (!this->download.valid()) {
            throw std::runtime_error("Tried to join a url download before starting it!");
        }
        this->download.get();
    }
};

/**
 * @brief Class for the `ohl-mods` mod "file".
 * @note Inheriting from the mod file base class for the merging logic.
 */
class mods_folder : public mod_file {
   public:
    virtual mod_file_identifier get_identifier(void) const {
        throw std::runtime_error("Mods folder should not be treated as a mod file!");
    }

    virtual std::wstring get_display_name(void) const {
        throw std::runtime_error("Mods folder should not be treated as a mod file!");
    }

    virtual void load(void) {
        LOGI << "[OHL] Loading mods folder";

        for (const auto& path : ohl::util::get_sorted_files_in_dir(mod_dir)) {
            this->register_remote_file(std::make_shared<mod_file_local>(path));
        }
    }

    TEST_CASE_CLASS("loader::mods_folder::load") {
        auto original_mod_dir = mod_dir;
        mod_dir = std::filesystem::path("tests") / "sorted_files";

        const std::vector<mod_file_identifier> expected_identifiers = {
            L"tests\\sorted_files\\1.txt",  L"tests\\sorted_files\\5.txt",
            L"tests\\sorted_files\\10.txt", L"tests\\sorted_files\\a.txt",
            L"tests\\sorted_files\\b.txt",  L"tests\\sorted_files\\c.txt",
        };

        SUBCASE("") {
            mods_folder folder;
            folder.load();
            folder.join();

            std::vector<mod_file_identifier> found_identifiers{};
            std::transform(folder.sections.begin(), folder.sections.end(),
                           std::back_inserter(found_identifiers), [](const auto& section) {
                               REQUIRE(std::holds_alternative<remote_mod_data>(section));
                               return std::get<remote_mod_data>(section).identifier;
                           });

            CHECK(ITERABLE_EQUAL(found_identifiers, expected_identifiers));
        }

        mod_dir = original_mod_dir;
    }
};

#pragma endregion

#pragma region Parsing

/**
 * @brief Parses a hotfix command, and appends it to the given mod data.
 *
 * @param line The line to parse, without leading whitespace.
 * @param data The mod data object to append hotfixes to.
 */
static void parse_and_append_hotfix_cmd(const std::wstring_view& line, mod_data& data) {
    auto key_end_pos = line.find_first_of(',');
    if (key_end_pos == std::wstring::npos) {
        return;
    }

    // SparkEarlyLevelPatchEntry,(1,11,0,MAPNAME),...
    //                          ^   ^ ^  ^      ^
    // key_end_pos -------------+   | |  |      |
    // type_start_pos --------------+ |  |      |
    // type_end_pos ------------------+  |      |
    // map_start_pos --------------------+      |
    // map_end_pos -----------------------------+

    std::wstring key(line, 0, key_end_pos);
    std::wstring value(line, key_end_pos + 1, std::string::npos);

    // Check if it's a type 11, and extract the map
    auto type_start_pos = line.find_first_of(',', key_end_pos + 1) + 1;
    auto type_end_pos = line.find_first_of(',', type_start_pos + 1);
    if (type_end_pos != std::string::npos
        && line.substr(type_start_pos, type_end_pos - type_start_pos) == L"11") {
        auto map_start_pos = line.find_first_of(',', type_end_pos + 1) + 1;
        auto map_end_pos = line.find_first_of(')', map_start_pos);
        if (map_end_pos != std::string::npos) {
            data.type_11_hotfixes.emplace_back(key, value);
            data.type_11_maps.emplace(line, map_start_pos, map_end_pos - map_start_pos);
            return;
        }
    }

    data.hotfixes.emplace_back(key, value);
}

TEST_CASE("loader::parse_and_append_hotfix_cmd") {
    mod_data data;

    parse_and_append_hotfix_cmd(L"Spark", data);
    parse_and_append_hotfix_cmd(L"SparkLevelPatchEntry", data);
    REQUIRE(data.is_empty());

    parse_and_append_hotfix_cmd(L"Spark,", data);
    REQUIRE(data.hotfixes.size() == 1);
    REQUIRE(data.hotfixes[0] == hotfix{L"Spark", L""});

    parse_and_append_hotfix_cmd(L"Spark,abcde", data);
    REQUIRE(data.hotfixes.size() == 2);
    REQUIRE(data.hotfixes[1] == hotfix{L"Spark", L"abcde"});

    parse_and_append_hotfix_cmd(L"Spark,(1,111,1,NotA),Type11", data);
    REQUIRE(data.hotfixes.size() == 3);
    REQUIRE(data.hotfixes[2] == hotfix{L"Spark", L"(1,111,1,NotA),Type11"});

    REQUIRE(data.type_11_hotfixes.empty());
    REQUIRE(data.type_11_maps.empty());

    parse_and_append_hotfix_cmd(L"SparkLevelPatchEntry,(1,11,0,MapName),/Is/Actually.A,Type,11",
                                data);
    REQUIRE(data.hotfixes.size() == 3);
    REQUIRE(data.type_11_hotfixes.size() == 1);
    REQUIRE(data.type_11_hotfixes[0]
            == hotfix{L"SparkLevelPatchEntry", L"(1,11,0,MapName),/Is/Actually.A,Type,11"});

    REQUIRE(data.type_11_maps.size() == 1);
    REQUIRE(data.type_11_maps.count(L"MapName") == 1);

    REQUIRE(data.news_items.size() == 0);
}

/**
 * @brief Extracts the next csv field from a line, unescaping it if necessary.
 * @note Callers must ensure the line is not empty.
 *
 * @param line A view of the current line. Will be modified to advance past the extracted field.
 * @return A new string containing the extracted/escaped field.
 */
static std::wstring extract_csv_escaped(std::wstring_view& line) {
    // If the field is not escaped, simple comma search + return
    // This will (deliberately) throw an out of range if the line is empty
    if (line[0] != '"') {
        auto comma_pos = line.find_first_of(',', 0);
        std::wstring ret(line, 0, comma_pos);

        if (comma_pos == std::string::npos) {
            line = L"";
        } else {
            line = line.substr(comma_pos + 1);
        }

        return ret;
    }

    std::wstringstream stream{};
    auto quoted_start_pos = 1;
    auto quoted_end_pos = std::string::npos;
    while (true) {
        quoted_end_pos = line.find_first_of('"', quoted_start_pos);
        stream << line.substr(quoted_start_pos, quoted_end_pos - quoted_start_pos);

        // If we reached the end of the line
        if (quoted_end_pos >= (line.size() - 1)) {
            break;
        }

        // If this is not an escaped quote
        if (line[quoted_end_pos + 1] != '"') {
            break;
        }

        stream << '"';
        // This might move past the end of the string, but will be caught on next loop
        quoted_start_pos = quoted_end_pos + 2;
    }

    auto comma_pos = line.find_first_of(',', quoted_end_pos);
    if (comma_pos == std::string::npos) {
        line = L"";
    } else {
        line = line.substr(comma_pos + 1);
    }

    return stream.str();
}

TEST_CASE("loader::extract_csv_escaped") {
    const std::wstring CSV_STRING = L"normal,,,\"quoted\",\"escaped,comma\",\"escaped\"\"quote\"";
    std::wstring_view csv{CSV_STRING};

    CHECK(extract_csv_escaped(csv) == L"normal");
    REQUIRE(csv == L",,\"quoted\",\"escaped,comma\",\"escaped\"\"quote\"");

    CHECK(extract_csv_escaped(csv) == L"");
    REQUIRE(csv == L",\"quoted\",\"escaped,comma\",\"escaped\"\"quote\"");
    CHECK(extract_csv_escaped(csv) == L"");
    REQUIRE(csv == L"\"quoted\",\"escaped,comma\",\"escaped\"\"quote\"");

    CHECK(extract_csv_escaped(csv) == L"quoted");
    REQUIRE(csv == L"\"escaped,comma\",\"escaped\"\"quote\"");

    CHECK(extract_csv_escaped(csv) == L"escaped,comma");
    REQUIRE(csv == L"\"escaped\"\"quote\"");

    CHECK(extract_csv_escaped(csv) == L"escaped\"quote");
    REQUIRE(csv == L"");
}

/**
 * @brief Parses a news item command, and appends it to the given mod data.
 *
 * @param line The line to parse, without leading whitespace.
 * @param data The mod data object to append hotfixes to.
 */
static void parse_and_append_news_item_cmd(const std::wstring_view& line, mod_data& data) {
    auto cmd_end_pos = line.find_first_of(',');
    auto csv_view = line.substr(cmd_end_pos + 1);

    std::wstring header;
    std::wstring image_url;
    std::wstring article_url;
    std::wstring body;

    header = extract_csv_escaped(csv_view);
    if (!csv_view.empty()) {
        image_url = extract_csv_escaped(csv_view);
        if (!csv_view.empty()) {
            article_url = extract_csv_escaped(csv_view);
            if (!csv_view.empty()) {
                body = std::wstring(csv_view);
            }
        }
    }

    data.news_items.emplace_back(header, image_url, article_url, body);
}

TEST_CASE("loader::parse_and_append_news_item_cmd") {
    mod_data data;

    parse_and_append_news_item_cmd(L"InjectNewsItem,Header", data);
    REQUIRE(data.news_items.size() == 1);
    REQUIRE(data.news_items[0] == news_item{L"Header"});

    parse_and_append_news_item_cmd(L"InjectNewsItem,  Header  ,image.png", data);
    REQUIRE(data.news_items.size() == 2);
    REQUIRE(data.news_items[1] == news_item{L"  Header  ", L"image.png"});

    parse_and_append_news_item_cmd(L"InjectNewsItem,Header,\"escaped,image.png\"", data);
    REQUIRE(data.news_items.size() == 3);
    REQUIRE(data.news_items[2] == news_item{L"Header", L"escaped,image.png"});

    parse_and_append_news_item_cmd(L"InjectNewsItem,\"Header\",\"escaped,image.png\"", data);
    REQUIRE(data.news_items.size() == 4);
    REQUIRE(data.news_items[3] == news_item{L"Header", L"escaped,image.png"});

    parse_and_append_news_item_cmd(L"InjectNewsItem,\"Header\",,Article", data);
    REQUIRE(data.news_items.size() == 5);
    REQUIRE(data.news_items[4] == news_item{L"Header", L"", L"Article"});

    parse_and_append_news_item_cmd(L"InjectNewsItem,, \t ,\"Article\"", data);
    REQUIRE(data.news_items.size() == 6);
    REQUIRE(data.news_items[5] == news_item{L"", L" \t ", L"Article"});

    parse_and_append_news_item_cmd(L"InjectNewsItem,Header,image.png,\"Article \"\"URL\"\"", data);
    REQUIRE(data.news_items.size() == 7);
    REQUIRE(data.news_items[6] == news_item{L"Header", L"image.png", L"Article \"URL\""});

    parse_and_append_news_item_cmd(L"InjectNewsItem,,image.png,Article,Body", data);
    REQUIRE(data.news_items.size() == 8);
    REQUIRE(data.news_items[7] == news_item{L"", L"image.png", L"Article", L"Body"});

    parse_and_append_news_item_cmd(
        L"InjectNewsItem,Header,image.png,Article,Body, with commas, and \"quotes\"", data);
    REQUIRE(data.news_items.size() == 9);
    REQUIRE(
        data.news_items[8]
        == news_item{L"Header", L"image.png", L"Article", L"Body, with commas, and \"quotes\""});

    parse_and_append_news_item_cmd(L"InjectNewsItem,,,,,,Body, with commas, and \"quotes\"", data);
    REQUIRE(data.news_items.size() == 10);
    REQUIRE(data.news_items[9] == news_item{L"", L"", L"", L",,Body, with commas, and \"quotes\""});

    REQUIRE(data.hotfixes.size() == 0);
    REQUIRE(data.type_11_hotfixes.size() == 0);
    REQUIRE(data.type_11_maps.size() == 0);
}

/**
 * @brief Parses an exec command and extracts the path to execute.
 *
 * @param line The line to parse, without leading whitespace.
 * @return The path to the file to execute, or std::nullopt if parsing failed.
 */
static std::optional<std::filesystem::path> parse_exec_cmd(const std::wstring_view& line) {
    auto whitespace_start = line.find_first_of(WHITESPACE);
    auto path_start = line.find_first_not_of(WHITESPACE, whitespace_start + 1);
    if (path_start == std::string::npos) {
        return std::nullopt;
    }

    // The standard exec command actually only grabs the first word
    // We will instead be permissive, and grab all text (except trailing whitespace), so that things
    //  just work more often - though strictly this shouldn't be considered a defined feature
    auto path_end = line.find_last_not_of(WHITESPACE);
    if (path_end == std::string::npos) {
        path_end = line.size() - 1;
    }

    // If someone quoted the path (e.g. for another, less permissive tool), strip quotes
    if ((line[path_start] == '"' && line[path_end] == '"')
        || (line[path_start] == '\'' && line[path_end] == '\'')) {
        path_start++;
        path_end--;
    }

    return (mod_dir / line.substr(path_start, (path_end + 1) - path_start)).lexically_normal();
}

TEST_CASE("loader::parse_exec_cmd") {
    CHECK(parse_exec_cmd(L"exec abc.bl3hotfix") == (mod_dir / "abc.bl3hotfix"));
    CHECK(parse_exec_cmd(L"exec   \t      abc.bl3hotfix      ") == (mod_dir / "abc.bl3hotfix"));
    CHECK(parse_exec_cmd(L"exec nested/path.bl3hotfix") == (mod_dir / "nested" / "path.bl3hotfix"));
    CHECK(parse_exec_cmd(L"exec D:\\absolute\\path.bl3hotfix")
          == std::filesystem::path("D:\\") / "absolute" / "path.bl3hotfix");
    CHECK(parse_exec_cmd(L"exec \"path with spaces.bl3hotfix\"")
          == (mod_dir / "path with spaces.bl3hotfix"));
    CHECK(parse_exec_cmd(L"exec  '  more spaces.bl3hotfix'")
          == (mod_dir / "  more spaces.bl3hotfix"));
}

/**
 * @brief Parses a url command.
 *
 * @param line The line to parse, without leading whitespace.
 * @return The parsed url, or std::nullopt if unable to parse.
 */
static std::optional<std::wstring> parse_url_cmd(const std::wstring_view& line) {
    return std::wstring(line, URL_COMMAND.size(), line.size() - URL_COMMAND.size());
}

TEST_CASE("loader::parse_url_cmd") {
    CHECK(parse_url_cmd(L"url=https://example.com/mod.bl3hotfix")
          == L"https://example.com/mod.bl3hotfix");
    CHECK(parse_url_cmd(L"url=  \"https://example.com/mod.bl3hotfix\"")
          == L"  \"https://example.com/mod.bl3hotfix\"");
    CHECK(parse_url_cmd(L"URL=1234") == L"1234");
}

/**
 * @brief Loads this mod file from a stream.
 *
 * @param stream The stream to read from.
 * @param allow_exec True if to allow running exec commands.
 */
void mod_file::load_from_stream(std::istream& stream, bool allow_exec) {
    mod_data data{};

    for (std::string narrow_mod_line; std::getline(stream, narrow_mod_line);) {
        std::wstring wide_mod_line = ohl::util::widen(narrow_mod_line);
        std::wstring_view mod_line{wide_mod_line};

        auto whitespace_end_pos = mod_line.find_first_not_of(WHITESPACE);
        if (whitespace_end_pos == std::wstring::npos) {
            continue;
        }
        mod_line = mod_line.substr(whitespace_end_pos);

        auto lower_mod_line = wide_mod_line;
        std::transform(lower_mod_line.begin(), lower_mod_line.end(), lower_mod_line.begin(),
                       std::towlower);

        /**
         * @brief Checks if the current line starts with the specified command.
         * @note Should compare against lowercase strings.
         *
         * @param cmd The command string to check.
         * @return True if the line starts with the command, false otherwise.
         */
        auto is_command = [&](auto cmd) {
            return lower_mod_line.compare(whitespace_end_pos, cmd.size(), cmd) == 0;
        };

        if (is_command(HOTFIX_COMMAND)) {
            parse_and_append_hotfix_cmd(mod_line, data);
        } else if (is_command(NEWS_COMMAND)) {
            parse_and_append_news_item_cmd(mod_line, data);
        } else if (allow_exec && is_command(EXEC_COMMAND)) {
            auto path = parse_exec_cmd(mod_line);

            if (path) {
                // Push our current data, and create a new one for use after loading the file.
                this->push_mod_data(data);
                this->register_remote_file(std::make_shared<mod_file_local>(*path));
                data = mod_data{};
            }
        } else if (is_command(URL_COMMAND)) {
            auto url = parse_url_cmd(mod_line);

            if (url) {
                this->push_mod_data(data);
                this->register_remote_file(std::make_shared<mod_file_url>(*url));
                data = mod_data{};
            }
        }
    }

    this->push_mod_data(data);
}

TEST_CASE("loader::mod_file_local::load") {
    const hotfix basic_mod_hotfix{
        L"SparkLevelPatchEntry",
        L"(1,1,1,Crypt_P),/Game/Cinematics/_Design/NPCs/"
        "BPCine_Actor_Typhon.BPCine_Actor_Typhon_C:SkeletalMesh_GEN_VARIABLE,"
        "bEnableUpdateRateOptimizations,4,True,False"};
    const hotfix unicode_statement_hotfix{L"SparkPatchEntry",
            (wchar_t*)u"(1,1,0,),/Game/PatchDLC/Raid1/Gear/Weapons/Link/"
                      u"Name_MAL_SM_Link.Name_MAL_SM_Link,PartName,0,,Cú Chulainn"};
    const hotfix sliding_hotfix{L"SparkPatchEntry",
                                L"(1,1,0,),/Game/PlayerCharacters/_Shared/_Design/Sliding/"
                                L"ControlledMove_Global_Sliding.Default__ControlledMove_Global_"
                                L"Sliding_C,Duration.BaseValueConstant,0,,5000"};

    const std::vector<std::pair<std::string, mod_data>> PARAMETERIZED_CASES{
        {"basic_mod.bl3hotfix", {{basic_mod_hotfix}, {}, {}, {}}},
        {"basic_mod.URL", {{basic_mod_hotfix}, {}, {}, {}}},
        {"unicode_statement.bl3hotfix", {{unicode_statement_hotfix}, {}, {}, {}}},
        {"easy_entry_to_fort_sunshine.bl3hotfix",
         {{{L"SparkEarlyLevelPatchEntry",
            L"(1,1,1,Wetlands_P),/Game/Maps/Zone_2/Wetlands/"
            L"Wetlands_P.Wetlands_P:PersistentLevel.IO_Switch_Industrial_Prison_C_0."
            L"DefaultSceneRoot,"
            L"RelativeLocation,0,,(X=34927.000000,Y=10709.000000,Z=3139.000000)"},
           {L"SparkEarlyLevelPatchEntry",
            L"(1,1,1,Wetlands_P),/Game/Maps/Zone_2/Wetlands/"
            L"Wetlands_P.Wetlands_P:PersistentLevel.IO_Switch_Industrial_Prison_C_0."
            L"DefaultSceneRoot,"
            L"RelativeRotation,0,,(Pitch=0.000000,Yaw=67.716370,Roll=0.000000)"},
           {L"SparkEarlyLevelPatchEntry",
            L"(1,1,1,Wetlands_P),/Game/Maps/Zone_2/Wetlands/"
            L"Wetlands_P.Wetlands_P:PersistentLevel.IO_Switch_Industrial_Prison_C_0."
            L"DefaultSceneRoot,"
            L"RelativeScale3D,0,,(X=1.000000,Y=1.000000,Z=1.000000)"},
           {L"SparkLevelPatchEntry",
            L"(1,1,0,Wetlands_P),/Game/Maps/Zone_2/Wetlands/"
            L"Wetlands_P.Wetlands_P:PersistentLevel.IO_Switch_Industrial_Prison_C_0,On_SwitchUsed,"
            L"0,,("
            L"Wetlands_M_EP12JakobsRebellion_C_11.BndEvt__IO_Switch_Circuit_Breaker_V_2_K2Node_"
            L"ActorBoundEvent_0_On_SwitchUsed__DelegateSignature)"},
           {L"SparkLevelPatchEntry",
            L"(1,1,0,Wetlands_P),/Game/Maps/Zone_2/Wetlands/"
            L"Wetlands_P.Wetlands_P:PersistentLevel.IO_Switch_Industrial_Prison_C_0,"
            L"SingleActivation,"
            L"0,,False"}},
          {{L"SparkEarlyLevelPatchEntry",
            L"(1,11,0,Wetlands_P),/Game/Maps/Zone_2/Wetlands,/Game/InteractiveObjects/Switches/"
            L"Lever/"
            L"Design,IO_Switch_Industrial_Prison,80,\"0.000000,0.000000,0.000000|0.000000,0.000000,"
            L"0."
            L"000000|1.000000,1.000000,1.000000\""}},
          {L"Wetlands_P"},
          {}}},
        {"single_exec.bl3hotfix", {{basic_mod_hotfix}, {}, {}, {}}},
        {"nested_exec.bl3hotfix", {{basic_mod_hotfix}, {}, {}, {}}},
        {"multi_exec.bl3hotfix",
         {{basic_mod_hotfix, unicode_statement_hotfix, sliding_hotfix}, {}, {}, {}}},
        {"multi_exec.URL", {{sliding_hotfix}, {}, {}, {}}},
        {"news.bl3hotfix", {{}, {}, {}, {{L"My News Item", L"", L"https://example.com"}}}},
    };

    auto original_mod_dir = mod_dir;
    mod_dir = std::filesystem::path("tests");

    std::string filename;
    mod_data expected_data;

    std::for_each(PARAMETERIZED_CASES.begin(), PARAMETERIZED_CASES.end(), [&](const auto& in) {
        SUBCASE(in.first.c_str()) {
            filename = in.first;
            expected_data = in.second;
        }
    });

    known_mod_files.clear();

    mod_file_local file{mod_dir / filename};
    file.load();

    mod_data data;
    file.append_to(data);

    CHECK(ITERABLE_EQUAL(data.hotfixes, expected_data.hotfixes));
    CHECK(ITERABLE_EQUAL(data.type_11_hotfixes, expected_data.type_11_hotfixes));
    CHECK(ITERABLE_EQUAL(data.type_11_maps, expected_data.type_11_maps));
    CHECK(ITERABLE_EQUAL(data.news_items, expected_data.news_items));

    mod_dir = original_mod_dir;
}

#pragma endregion

/**
 * @brief Creates the news item for OHL.
 *
 * @param hotfix_count The amount of injected hotfixes
 * @param file_order The injected mod files, in the order they were loaded.
 * @return The OHL news item.
 */
static news_item get_ohl_news_item(size_t hotfix_count,
                                   std::vector<std::shared_ptr<mod_file>> file_order) {
    // If we're in BL3, colour the name.
    // WL doesn't support font tags :(
    std::wstring ohl_name;
    if (ohl::args::exe_path().stem() == "Borderlands3") {
        ohl_name =
            L"<font color='#0080E0'>OHL</font><font size='14' color='#C0C0C0'> " VERSION_STRING
            "</font>";
    } else {
        ohl_name = L"OHL " VERSION_STRING;
    }

    std::wstring header;
    if (hotfix_count > 1) {
        header = ohl_name + L": " + std::to_wstring(hotfix_count) + L" hotfixes loaded";
    } else if (hotfix_count == 1) {
        header = ohl_name + L": 1 hotfix loaded";
    } else {
        header = ohl_name + L": No hotfixes loaded";
    }

    std::wstring body;
    if (hotfix_count == 0) {
        body = L"No hotfixes loaded";
    } else {
        std::wstringstream stream{};
        stream << L"Loaded files:\n";

        for (const auto& file : file_order) {
            stream << file->get_display_name() << L",\n";
        }

        body = stream.str();
    }

    return {header, OHL_NEWS_ITEM_IMAGE_URL, OHL_NEWS_ITEM_ARTICLE_URL, body};
}

#pragma region Public interface

static std::mutex reloading_mutex;
static mod_data loaded_mod_data;

/**
 * @brief Implementation of `reload`, which reloads the hotfix list.
 * @note Intended to be run in a thread.
 */
static void reload_impl(void) {
    std::lock_guard<std::mutex> lock(reloading_mutex);

    SetThreadDescription(GetCurrentThread(), L"OpenHotfixLoader Loader");

    // If the mod folder doesn't exist, create it, and then just quit early since we know we won't
    //  load anything
    if (!std::filesystem::exists(mod_dir)) {
        std::filesystem::create_directories(mod_dir);
        return;
    }

    // No need to lock here since we haven't started loading
    known_mod_files.clear();

    mods_folder folder_data{};
    folder_data.load();

    LOGD << "[OHL] Combining mod data";
    mod_data combined_mod_data{};
    std::vector<mod_file_identifier> seen_files;
    folder_data.append_to(combined_mod_data, seen_files);

    LOGD << "[OHL] Processing type 11s";

    // Add type 11s to the front of the list, and their delays after them but before the rest
    for (const auto& map : combined_mod_data.type_11_maps) {
        static const auto map_start_pos = TYPE_11_DELAY_VALUE.find(L"{map}");
        static const auto map_length = 5;
        static const auto mesh_start_pos = TYPE_11_DELAY_VALUE.find(L"{mesh}");
        static const auto mesh_length = 6;

        for (const auto& mesh : TYPE_11_DELAY_MESHES) {
            // Make sure to replace mesh first, since it appears later in the string
            auto hotfix = std::wstring(TYPE_11_DELAY_VALUE)
                              .replace(mesh_start_pos, mesh_length, mesh)
                              .replace(map_start_pos, map_length, map);
            combined_mod_data.type_11_hotfixes.emplace_back(TYPE_11_DELAY_TYPE, hotfix);
        }
    }
    for (auto it = combined_mod_data.type_11_hotfixes.rbegin();
         it != combined_mod_data.type_11_hotfixes.rend(); it++) {
        combined_mod_data.hotfixes.push_front(*it);
    }

    LOGD << "[OHL] Adding OHL news item";

    std::vector<std::shared_ptr<mod_file>> file_order;
    for (const auto& identifier : seen_files) {
        auto file = known_mod_files.at(identifier);
        if (file->sections.size() == 0) {
            continue;
        }

        // Special case ignoring a file holding a single remote url reference - i.e. the url
        //  shortcut files we'll be seeing in 99% of cases
        if (file->sections.size() == 1
            && std::holds_alternative<remote_mod_data>(file->sections[0])) {
            auto remote_file =
                known_mod_files.at(std::get<remote_mod_data>(file->sections[0]).identifier);
            if (dynamic_cast<mod_file_url*>(remote_file.get()) != nullptr) {
                continue;
            }
        }

        file_order.push_back(file);
    }

    combined_mod_data.news_items.push_front(
        get_ohl_news_item(combined_mod_data.hotfixes.size(), file_order));

    LOGD << "[OHL] Replacing globals";

    loaded_mod_data = combined_mod_data;

    LOGI << "[OHL] Loading finished, loaded files:";
    for (const auto& file : file_order) {
        LOGI << "[OHL] " << file->get_display_name();
    }
}

void init(void) {
    auto dll_path = ohl::args::dll_path();
    if (std::filesystem::exists(dll_path)) {
        mod_dir = dll_path.remove_filename() / mod_dir;
    }
}

void reload(void) {
    std::thread thread(reload_impl);
    thread.detach();
}

std::deque<hotfix> get_hotfixes(void) {
    std::lock_guard<std::mutex> lock(reloading_mutex);

    return loaded_mod_data.hotfixes;
}

std::deque<news_item> get_news_items(void) {
    std::lock_guard<std::mutex> lock(reloading_mutex);

    return loaded_mod_data.news_items;
}

#pragma endregion

TEST_SUITE_END();
}  // namespace ohl::loader

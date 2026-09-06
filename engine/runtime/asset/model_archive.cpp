#include "model_archive.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <cstring>
#include <fstream>
#include <limits>
#include <sstream>
#include <utility>

#ifndef KPENGINE_MODEL_ARCHIVE_PRODUCT_ONLY
#include "database/database.h"
#endif

namespace kpengine::asset
{
    namespace
    {
        constexpr std::array<std::uint32_t, 64> kRoundConstants{
            0x428a2f98u, 0x71374491u, 0xb5c0fbcfu, 0xe9b5dba5u, 0x3956c25bu,
            0x59f111f1u, 0x923f82a4u, 0xab1c5ed5u, 0xd807aa98u, 0x12835b01u,
            0x243185beu, 0x550c7dc3u, 0x72be5d74u, 0x80deb1feu, 0x9bdc06a7u,
            0xc19bf174u, 0xe49b69c1u, 0xefbe4786u, 0x0fc19dc6u, 0x240ca1ccu,
            0x2de92c6fu, 0x4a7484aau, 0x5cb0a9dcu, 0x76f988dau, 0x983e5152u,
            0xa831c66du, 0xb00327c8u, 0xbf597fc7u, 0xc6e00bf3u, 0xd5a79147u,
            0x06ca6351u, 0x14292967u, 0x27b70a85u, 0x2e1b2138u, 0x4d2c6dfcu,
            0x53380d13u, 0x650a7354u, 0x766a0abbu, 0x81c2c92eu, 0x92722c85u,
            0xa2bfe8a1u, 0xa81a664bu, 0xc24b8b70u, 0xc76c51a3u, 0xd192e819u,
            0xd6990624u, 0xf40e3585u, 0x106aa070u, 0x19a4c116u, 0x1e376c08u,
            0x2748774cu, 0x34b0bcb5u, 0x391c0cb3u, 0x4ed8aa4au, 0x5b9cca4fu,
            0x682e6ff3u, 0x748f82eeu, 0x78a5636fu, 0x84c87814u, 0x8cc70208u,
            0x90befffau, 0xa4506cebu, 0xbef9a3f7u, 0xc67178f2u};

        constexpr std::array<std::uint32_t, 8> kInitialHash{
            0x6a09e667u, 0xbb67ae85u, 0x3c6ef372u, 0xa54ff53au,
            0x510e527fu, 0x9b05688cu, 0x1f83d9abu, 0x5be0cd19u};

        constexpr std::uint32_t RotateRight(std::uint32_t value, unsigned count) noexcept
        {
            return (value >> count) | (value << (32u - count));
        }

        class Sha256State final
        {
        public:
            Sha256State() : state_(kInitialHash)
            {
            }

            void Update(const std::uint8_t *data, std::size_t size)
            {
                if (data == nullptr && size != 0)
                {
                    throw ModelArchiveError(ModelArchiveErrorCode::InvalidArgument,
                                            "SHA-256 input pointer is null");
                }
                bit_count_ += static_cast<std::uint64_t>(size) * 8u;
                while (size != 0)
                {
                    const std::size_t copied =
                        std::min(size, block_.size() - block_size_);
                    std::memcpy(block_.data() + block_size_, data, copied);
                    block_size_ += copied;
                    data += copied;
                    size -= copied;
                    if (block_size_ == block_.size())
                    {
                        Transform(block_);
                        block_size_ = 0;
                    }
                }
            }

            ContentHash Final()
            {
                const std::uint64_t original_bit_count = bit_count_;
                std::array<std::uint8_t, 128> padding{};
                padding[0] = 0x80u;
                const std::size_t padding_size = block_size_ < 56 ? 56 - block_size_
                                                                    : 120 - block_size_;
                Update(padding.data(), padding_size);
                std::array<std::uint8_t, 8> length{};
                for (std::size_t index = 0; index < length.size(); ++index)
                {
                    length[length.size() - index - 1] = static_cast<std::uint8_t>(
                        (original_bit_count >> (index * 8u)) & 0xffu);
                }
                Update(length.data(), length.size());

                ContentHash result;
                for (std::size_t index = 0; index < state_.size(); ++index)
                {
                    result.bytes[index * 4] = static_cast<std::uint8_t>(state_[index] >> 24);
                    result.bytes[index * 4 + 1] = static_cast<std::uint8_t>(state_[index] >> 16);
                    result.bytes[index * 4 + 2] = static_cast<std::uint8_t>(state_[index] >> 8);
                    result.bytes[index * 4 + 3] = static_cast<std::uint8_t>(state_[index]);
                }
                return result;
            }

        private:
            void Transform(const std::array<std::uint8_t, 64> &block)
            {
                std::array<std::uint32_t, 64> words{};
                for (std::size_t index = 0; index < 16; ++index)
                {
                    words[index] = (static_cast<std::uint32_t>(block[index * 4]) << 24) |
                                   (static_cast<std::uint32_t>(block[index * 4 + 1]) << 16) |
                                   (static_cast<std::uint32_t>(block[index * 4 + 2]) << 8) |
                                   static_cast<std::uint32_t>(block[index * 4 + 3]);
                }
                for (std::size_t index = 16; index < words.size(); ++index)
                {
                    const std::uint32_t s0 = RotateRight(words[index - 15], 7) ^
                                             RotateRight(words[index - 15], 18) ^
                                             (words[index - 15] >> 3);
                    const std::uint32_t s1 = RotateRight(words[index - 2], 17) ^
                                             RotateRight(words[index - 2], 19) ^
                                             (words[index - 2] >> 10);
                    words[index] = words[index - 16] + s0 + words[index - 7] + s1;
                }

                std::uint32_t a = state_[0];
                std::uint32_t b = state_[1];
                std::uint32_t c = state_[2];
                std::uint32_t d = state_[3];
                std::uint32_t e = state_[4];
                std::uint32_t f = state_[5];
                std::uint32_t g = state_[6];
                std::uint32_t h = state_[7];
                for (std::size_t index = 0; index < words.size(); ++index)
                {
                    const std::uint32_t s1 = RotateRight(e, 6) ^ RotateRight(e, 11) ^
                                             RotateRight(e, 25);
                    const std::uint32_t choice = (e & f) ^ ((~e) & g);
                    const std::uint32_t temporary1 = h + s1 + choice + kRoundConstants[index] +
                                                     words[index];
                    const std::uint32_t s0 = RotateRight(a, 2) ^ RotateRight(a, 13) ^
                                             RotateRight(a, 22);
                    const std::uint32_t majority = (a & b) ^ (a & c) ^ (b & c);
                    const std::uint32_t temporary2 = s0 + majority;
                    h = g;
                    g = f;
                    f = e;
                    e = d + temporary1;
                    d = c;
                    c = b;
                    b = a;
                    a = temporary1 + temporary2;
                }
                state_[0] += a;
                state_[1] += b;
                state_[2] += c;
                state_[3] += d;
                state_[4] += e;
                state_[5] += f;
                state_[6] += g;
                state_[7] += h;
            }

            std::array<std::uint32_t, 8> state_{};
            std::array<std::uint8_t, 64> block_{};
            std::size_t block_size_{};
            std::uint64_t bit_count_{};
        };

        ContentHash HashBytes(const std::uint8_t *data, std::size_t size)
        {
            Sha256State state;
            state.Update(data, size);
            return state.Final();
        }

#ifndef KPENGINE_MODEL_ARCHIVE_PRODUCT_ONLY
        std::vector<std::byte> ToBlob(const ContentHash &hash)
        {
            std::vector<std::byte> result;
            result.reserve(hash.bytes.size());
            for (const std::uint8_t value : hash.bytes)
            {
                result.push_back(static_cast<std::byte>(value));
            }
            return result;
        }

        ContentHash FromBlob(const std::vector<std::byte> &blob)
        {
            if (blob.size() != kModelArchiveHashSize)
            {
                throw ModelArchiveError(ModelArchiveErrorCode::InvalidDatabase,
                                        "archive hash column is not 32 bytes");
            }
            ContentHash result;
            for (std::size_t index = 0; index < result.bytes.size(); ++index)
            {
                result.bytes[index] = static_cast<std::uint8_t>(blob[index]);
            }
            return result;
        }
#endif

        void AppendU32(std::vector<std::uint8_t> &bytes, std::uint32_t value)
        {
            for (unsigned shift = 0; shift < 32; shift += 8)
            {
                bytes.push_back(static_cast<std::uint8_t>((value >> shift) & 0xffu));
            }
        }

        void AppendU64(std::vector<std::uint8_t> &bytes, std::uint64_t value)
        {
            for (unsigned shift = 0; shift < 64; shift += 8)
            {
                bytes.push_back(static_cast<std::uint8_t>((value >> shift) & 0xffu));
            }
        }

        void AppendString(std::vector<std::uint8_t> &bytes, std::string_view value)
        {
            AppendU64(bytes, static_cast<std::uint64_t>(value.size()));
            bytes.insert(bytes.end(), value.begin(), value.end());
        }

        void AppendHash(std::vector<std::uint8_t> &bytes, const ContentHash &hash)
        {
            bytes.insert(bytes.end(), hash.bytes.begin(), hash.bytes.end());
        }

        std::string ProductDirectory(ArchiveProductType type)
        {
            switch (type)
            {
            case ArchiveProductType::Model:
                return "models";
            case ArchiveProductType::Material:
                return "materials";
            case ArchiveProductType::Texture:
                return "textures";
            }
            throw ModelArchiveError(ModelArchiveErrorCode::InvalidArgument,
                                    "unknown archive product type");
        }

        std::string ProductExtension(ArchiveProductType type, std::string_view texture_extension)
        {
            switch (type)
            {
            case ArchiveProductType::Model:
                return ".model";
            case ArchiveProductType::Material:
                return ".material";
            case ArchiveProductType::Texture:
                if (texture_extension.empty())
                {
                    throw ModelArchiveError(ModelArchiveErrorCode::InvalidArgument,
                                            "texture product extension is empty");
                }
                for (const unsigned char character : texture_extension)
                {
                    if (!std::isalnum(character))
                    {
                        throw ModelArchiveError(ModelArchiveErrorCode::InvalidArgument,
                                                "texture product extension is not portable");
                    }
                }
                {
                    std::string extension;
                    extension.reserve(texture_extension.size());
                    for (const unsigned char character : texture_extension)
                    {
                        extension.push_back(static_cast<char>(std::tolower(character)));
                    }
                    return "." + extension;
                }
            }
            throw ModelArchiveError(ModelArchiveErrorCode::InvalidArgument,
                                    "unknown archive product type");
        }

#ifndef KPENGINE_MODEL_ARCHIVE_PRODUCT_ONLY
        std::string NormalizeArchiveRelativePath(std::string_view value)
        {
            return NormalizeAssetRelativePath(value);
        }

        [[noreturn]] void ThrowDatabaseError(const database::DatabaseError &error)
        {
            if (error.IsBusy() || error.IsLocked())
            {
                throw ModelArchiveError(ModelArchiveErrorCode::ArchiveBusy,
                                        "archive database is busy: " + std::string{error.what()},
                                        error.ResultCode());
            }
            const std::string message{error.what()};
            if (message.find("not a database") != std::string::npos ||
                message.find("malformed") != std::string::npos)
            {
                throw ModelArchiveError(ModelArchiveErrorCode::InvalidDatabase, message,
                                        error.ResultCode());
            }
            throw ModelArchiveError(ModelArchiveErrorCode::IoError, message,
                                    error.ResultCode());
        }

        template <typename Function>
        decltype(auto) CatchDatabaseErrors(Function &&function)
        {
            try
            {
                return function();
            }
            catch (const database::DatabaseError &error)
            {
                ThrowDatabaseError(error);
            }
        }

        void ValidateHashPath(const ProductRecord &product)
        {
            const std::string normalized = NormalizeArchiveRelativePath(product.relative_path);
            if (normalized != product.relative_path)
            {
                throw ModelArchiveError(ModelArchiveErrorCode::InvalidArgument,
                                        "product path is not canonical: " + product.relative_path);
            }
            std::string texture_extension;
            if (product.asset_type == ArchiveProductType::Texture)
            {
                texture_extension = std::filesystem::path(product.relative_path)
                                        .extension()
                                        .string();
                if (!texture_extension.empty() && texture_extension.front() == '.')
                {
                    texture_extension.erase(0, 1);
                }
            }
            const std::string expected =
                ProductRelativePath(product.asset_type, product.content_hash, texture_extension);
            if (product.relative_path != expected)
            {
                throw ModelArchiveError(ModelArchiveErrorCode::InvalidArgument,
                                        "product path does not match its content hash");
            }
        }

        std::string ProbeDiagnostic(ArchiveProbeStatus status)
        {
            switch (status)
            {
            case ArchiveProbeStatus::SourceNotFound:
                return "source is not present in the archive";
            case ArchiveProbeStatus::SourceFailed:
                return "source archive record is failed";
            case ArchiveProbeStatus::SourcePackageChanged:
                return "source package fingerprint changed";
            case ArchiveProbeStatus::ImporterChanged:
                return "importer identity changed";
            case ArchiveProbeStatus::SettingsChanged:
                return "import settings fingerprint changed";
            case ArchiveProbeStatus::NativeSchemaChanged:
                return "native model schema changed";
            case ArchiveProbeStatus::MissingProduct:
                return "referenced archive product is missing";
            case ArchiveProbeStatus::CorruptProduct:
                return "referenced archive product failed hash verification";
            case ArchiveProbeStatus::UpToDate:
                return "source and products are verified and up to date";
            }
            return "unknown archive probe result";
        }
#endif
    }

#ifndef KPENGINE_MODEL_ARCHIVE_DATABASE_ONLY
    std::string ContentHash::ToHex() const
    {
        static constexpr char digits[] = "0123456789abcdef";
        std::string result;
        result.reserve(bytes.size() * 2);
        for (const std::uint8_t value : bytes)
        {
            result.push_back(digits[(value >> 4) & 0xf]);
            result.push_back(digits[value & 0xf]);
        }
        return result;
    }

    std::optional<ContentHash> ContentHash::FromHex(std::string_view value)
    {
        if (value.size() != kModelArchiveHashSize * 2)
        {
            return std::nullopt;
        }
        ContentHash result;
        for (std::size_t index = 0; index < result.bytes.size(); ++index)
        {
            auto digit = [](char character) -> int
            {
                if (character >= '0' && character <= '9')
                {
                    return character - '0';
                }
                if (character >= 'a' && character <= 'f')
                {
                    return character - 'a' + 10;
                }
                if (character >= 'A' && character <= 'F')
                {
                    return character - 'A' + 10;
                }
                return -1;
            };
            const int high = digit(value[index * 2]);
            const int low = digit(value[index * 2 + 1]);
            if (high < 0 || low < 0)
            {
                return std::nullopt;
            }
            result.bytes[index] = static_cast<std::uint8_t>((high << 4) | low);
        }
        return result;
    }

    ModelArchiveError::ModelArchiveError(ModelArchiveErrorCode code, std::string message,
                                         int sqlite_result_code)
        : std::runtime_error(std::move(message)),
          code_(code),
          sqlite_result_code_(sqlite_result_code)
    {
    }

    ModelArchiveErrorCode ModelArchiveError::Code() const noexcept
    {
        return code_;
    }

    int ModelArchiveError::SqliteResultCode() const noexcept
    {
        return sqlite_result_code_;
    }

    ContentHash Sha256(std::string_view value)
    {
        return HashBytes(reinterpret_cast<const std::uint8_t *>(value.data()), value.size());
    }

    ContentHash Sha256(const std::vector<std::byte> &value)
    {
        return HashBytes(reinterpret_cast<const std::uint8_t *>(value.data()), value.size());
    }

    ContentHash Sha256File(const std::filesystem::path &path)
    {
        std::ifstream file(path, std::ios::binary);
        if (!file.is_open())
        {
            throw ModelArchiveError(ModelArchiveErrorCode::IoError,
                                    "failed to open file for SHA-256: " + path.string());
        }

        Sha256State state;
        std::array<std::uint8_t, 64 * 1024> buffer{};
        while (file)
        {
            file.read(reinterpret_cast<char *>(buffer.data()),
                      static_cast<std::streamsize>(buffer.size()));
            const std::streamsize count = file.gcount();
            if (count > 0)
            {
                state.Update(buffer.data(), static_cast<std::size_t>(count));
            }
        }
        if (!file.eof())
        {
            throw ModelArchiveError(ModelArchiveErrorCode::IoError,
                                    "failed while reading file for SHA-256: " + path.string());
        }
        return state.Final();
    }

    bool VerifyArchiveProduct(const std::filesystem::path &path,
                              ArchiveProductType type,
                              const std::vector<std::byte> &bytes,
                              std::string &diagnostic,
                              const std::filesystem::path &product_root)
    {
        const std::filesystem::path normalized = path.lexically_normal();
        const std::string expected_directory = ProductDirectory(type);
        const std::string expected_extension = ProductExtension(type, {});
        const std::filesystem::path parent = normalized.parent_path();

        if (!product_root.empty())
        {
            const std::filesystem::path normalized_root = product_root.lexically_normal();
            if (parent != normalized_root / expected_directory)
            {
                diagnostic = "archive product is outside the supplied product root or has a "
                             "non-canonical layout";
                return false;
            }
        }
        else
        {
            bool under_archive = false;
            for (const auto &component : normalized)
            {
                if (component.generic_string() == ".archive")
                {
                    under_archive = true;
                    break;
                }
            }
            if (!under_archive)
            {
                diagnostic.clear();
                return true;
            }
            if (parent.filename().generic_string() != expected_directory ||
                parent.parent_path().filename().generic_string() != ".archive")
            {
                diagnostic = "archive product is not in the canonical .archive/" +
                             expected_directory + " layout";
                return false;
            }
        }
        if (normalized.extension().generic_string() != expected_extension)
        {
            diagnostic = "archive product has an invalid extension";
            return false;
        }

        const std::string stem = normalized.stem().generic_string();
        const std::optional<ContentHash> filename_hash = ContentHash::FromHex(stem);
        if (!filename_hash || filename_hash->ToHex() != stem)
        {
            diagnostic = "archive product filename is not a lowercase SHA-256";
            return false;
        }
        const ContentHash content_hash = Sha256(bytes);
        if (content_hash != *filename_hash)
        {
            diagnostic = "archive product filename does not match its bytes";
            return false;
        }

        diagnostic.clear();
        return true;
    }

    std::string NormalizeAssetRelativePath(std::string_view path)
    {
        if (path.empty() || path.find('\0') != std::string_view::npos)
        {
            throw ModelArchiveError(ModelArchiveErrorCode::InvalidArgument,
                                    "Asset-relative path is empty or contains NUL");
        }

        std::string portable{path};
        std::replace(portable.begin(), portable.end(), '\\', '/');
        const std::filesystem::path candidate{portable};
        if (candidate.is_absolute() || candidate.has_root_name() || candidate.has_root_directory())
        {
            throw ModelArchiveError(ModelArchiveErrorCode::InvalidArgument,
                                    "Asset-relative path is absolute: " + portable);
        }

        std::vector<std::string> segments;
        std::size_t begin = 0;
        while (begin <= portable.size())
        {
            const std::size_t end = portable.find('/', begin);
            const std::string segment = portable.substr(
                begin, end == std::string::npos ? std::string::npos : end - begin);
            if (segment == "..")
            {
                if (segments.empty())
                {
                    throw ModelArchiveError(ModelArchiveErrorCode::InvalidArgument,
                                            "Asset-relative path escapes its root: " + portable);
                }
                segments.pop_back();
            }
            else if (!segment.empty() && segment != ".")
            {
                segments.push_back(segment);
            }
            if (end == std::string::npos)
            {
                break;
            }
            begin = end + 1;
        }

        if (segments.empty())
        {
            throw ModelArchiveError(ModelArchiveErrorCode::InvalidArgument,
                                    "Asset-relative path has no file component");
        }

        std::string result;
        for (const std::string &segment : segments)
        {
            if (!result.empty())
            {
                result.push_back('/');
            }
            for (const unsigned char character : segment)
            {
                result.push_back(static_cast<char>(std::tolower(character)));
            }
        }
        return result;
    }

    ContentHash HashSourcePackage(const std::vector<SourceFingerprintInput> &files)
    {
        if (files.empty())
        {
            throw ModelArchiveError(ModelArchiveErrorCode::InvalidArgument,
                                    "source package has no files");
        }

        std::vector<SourceFingerprintInput> sorted = files;
        for (SourceFingerprintInput &file : sorted)
        {
            file.normalized_path = NormalizeAssetRelativePath(file.normalized_path);
        }
        std::sort(sorted.begin(), sorted.end(),
                  [](const SourceFingerprintInput &lhs, const SourceFingerprintInput &rhs)
                  { return lhs.normalized_path < rhs.normalized_path; });
        for (std::size_t index = 1; index < sorted.size(); ++index)
        {
            if (sorted[index - 1].normalized_path == sorted[index].normalized_path)
            {
                throw ModelArchiveError(ModelArchiveErrorCode::InvalidArgument,
                                        "source package contains a duplicate dependency path");
            }
        }

        std::vector<std::uint8_t> canonical;
        AppendString(canonical, "KPENGINE_SOURCE_PACKAGE_V1");
        AppendU64(canonical, static_cast<std::uint64_t>(sorted.size()));
        for (const SourceFingerprintInput &file : sorted)
        {
            AppendString(canonical, file.normalized_path);
            AppendHash(canonical, file.content_hash);
        }
        return HashBytes(canonical.data(), canonical.size());
    }

    ContentHash HashImportKey(const ImportKeyInput &input)
    {
        if (input.importer_id.empty())
        {
            throw ModelArchiveError(ModelArchiveErrorCode::InvalidArgument,
                                    "importer identity is empty");
        }
        std::vector<std::uint8_t> canonical;
        AppendString(canonical, "KPENGINE_MODEL_IMPORT_KEY_V1");
        AppendHash(canonical, input.source_package_hash);
        AppendString(canonical, input.importer_id);
        AppendU32(canonical, input.importer_version);
        AppendHash(canonical, input.settings_hash);
        AppendU32(canonical, input.native_model_version);
        AppendU32(canonical, input.material_schema_version);
        return HashBytes(canonical.data(), canonical.size());
    }

    std::string ProductRelativePath(ArchiveProductType type, const ContentHash &content_hash,
                                    std::string_view texture_extension)
    {
        return ProductDirectory(type) + "/" + content_hash.ToHex() +
               ProductExtension(type, texture_extension);
    }
#endif

#ifndef KPENGINE_MODEL_ARCHIVE_PRODUCT_ONLY
    struct ModelArchiveDatabase::Impl
    {
        explicit Impl(std::unique_ptr<database::Database> database_value)
            : database(std::move(database_value))
        {
        }

        std::unique_ptr<database::Database> database;
    };

    ModelArchiveDatabase::ModelArchiveDatabase(std::filesystem::path database_path,
                                               std::int32_t busy_timeout_ms,
                                               ModelArchiveOpenMode open_mode)
        : database_path_(std::move(database_path)),
          open_mode_(open_mode)
    {
        Initialize(busy_timeout_ms);
    }

    ModelArchiveDatabase::~ModelArchiveDatabase() noexcept = default;

    ModelArchiveDatabase::ModelArchiveDatabase(ModelArchiveDatabase &&other) noexcept = default;

    ModelArchiveDatabase &ModelArchiveDatabase::operator=(ModelArchiveDatabase &&other) noexcept =
        default;

    const std::filesystem::path &ModelArchiveDatabase::DatabasePath() const noexcept
    {
        return database_path_;
    }

    std::filesystem::path ModelArchiveDatabase::ArchiveRoot() const
    {
        return database_path_.parent_path();
    }

    std::int32_t ModelArchiveDatabase::SchemaVersion() const noexcept
    {
        return kSchemaVersion;
    }

    void ModelArchiveDatabase::Initialize(std::int32_t busy_timeout_ms)
    {
        if (busy_timeout_ms < 0)
        {
            throw ModelArchiveError(ModelArchiveErrorCode::InvalidArgument,
                                    "SQLite busy timeout cannot be negative");
        }
        try
        {
            impl_ = std::make_unique<Impl>(std::make_unique<database::Database>(
                database_path_.string(),
                open_mode_ == ModelArchiveOpenMode::ReadOnly
                    ? database::DatabaseOpenMode::ReadOnly
                    : database::DatabaseOpenMode::ReadWriteCreate));
        }
        catch (const database::DatabaseError &error)
        {
            ThrowDatabaseError(error);
        }
        ConfigureConnection(busy_timeout_ms);
        EnsureSchema();
    }

    void ModelArchiveDatabase::ConfigureConnection(std::int32_t busy_timeout_ms)
    {
        CatchDatabaseErrors([&]
        {
            impl_->database->Execute("PRAGMA foreign_keys=ON;");
            impl_->database->Execute("PRAGMA busy_timeout=" + std::to_string(busy_timeout_ms) + ";");
            if (open_mode_ == ModelArchiveOpenMode::ReadOnly)
            {
                impl_->database->Execute("PRAGMA query_only=ON;");
            }
            else
            {
                impl_->database->Execute("PRAGMA journal_mode=WAL;");
                impl_->database->Execute("PRAGMA synchronous=FULL;");
            }
        });
    }

    void ModelArchiveDatabase::EnsureSchema()
    {
        const auto read_user_version = [&]() -> std::int32_t
        {
            auto statement = impl_->database->Prepare("PRAGMA user_version;");
            if (statement.Step() != database::StatementStep::Row)
            {
                throw ModelArchiveError(ModelArchiveErrorCode::InvalidDatabase,
                                        "archive database did not return user_version");
            }
            return static_cast<std::int32_t>(statement.ColumnInt64(0));
        };

        const std::int32_t version = CatchDatabaseErrors(read_user_version);
        if (version > kSchemaVersion)
        {
            throw ModelArchiveError(ModelArchiveErrorCode::NewerSchema,
                                    "archive database schema is newer than this importer");
        }
        if (version == 0 && open_mode_ == ModelArchiveOpenMode::ReadOnly)
        {
            throw ModelArchiveError(ModelArchiveErrorCode::InvalidDatabase,
                                    "read-only archive has no initialized schema");
        }
        if (version == 0)
        {
            CatchDatabaseErrors([&]
            {
                database::Transaction transaction{*impl_->database};
                impl_->database->Execute(
                    "CREATE TABLE IF NOT EXISTS archive_meta ("
                    "key TEXT PRIMARY KEY, value TEXT NOT NULL);"
                    "CREATE TABLE IF NOT EXISTS sources ("
                    "id INTEGER PRIMARY KEY, normalized_path TEXT NOT NULL UNIQUE, "
                    "path_hash BLOB NOT NULL UNIQUE CHECK(length(path_hash)=32), "
                    "display_name TEXT NOT NULL, "
                    "package_hash BLOB NOT NULL CHECK(length(package_hash)=32), "
                    "importer_id TEXT NOT NULL, importer_version INTEGER NOT NULL, "
                    "settings_hash BLOB NOT NULL CHECK(length(settings_hash)=32), "
                    "native_model_version INTEGER NOT NULL, status INTEGER NOT NULL, "
                    "diagnostic TEXT NOT NULL DEFAULT '');"
                    "CREATE TABLE IF NOT EXISTS source_dependencies ("
                    "source_id INTEGER NOT NULL REFERENCES sources(id) ON DELETE CASCADE, "
                    "normalized_path TEXT NOT NULL, "
                    "content_hash BLOB NOT NULL CHECK(length(content_hash)=32), "
                    "PRIMARY KEY(source_id, normalized_path));"
                    "CREATE TABLE IF NOT EXISTS products ("
                    "content_hash BLOB NOT NULL CHECK(length(content_hash)=32), "
                    "asset_type INTEGER NOT NULL, relative_path TEXT NOT NULL UNIQUE, "
                    "byte_size INTEGER NOT NULL, schema_version INTEGER NOT NULL, "
                    "PRIMARY KEY(content_hash, asset_type));"
                    "CREATE TABLE IF NOT EXISTS source_products ("
                    "source_id INTEGER NOT NULL REFERENCES sources(id) ON DELETE CASCADE, "
                    "content_hash BLOB NOT NULL, asset_type INTEGER NOT NULL, "
                    "role INTEGER NOT NULL, slot INTEGER NOT NULL DEFAULT -1, "
                    "display_name TEXT NOT NULL, PRIMARY KEY(source_id, role, slot), "
                    "FOREIGN KEY(content_hash, asset_type) REFERENCES products(content_hash, asset_type));"
                    "CREATE TABLE IF NOT EXISTS material_overrides ("
                    "source_id INTEGER NOT NULL REFERENCES sources(id) ON DELETE CASCADE, "
                    "slot INTEGER NOT NULL, authored_path TEXT NOT NULL, "
                    "PRIMARY KEY(source_id, slot));"
                    "INSERT OR IGNORE INTO archive_meta(key, value) VALUES "
                    "('hash_algorithm', 'SHA-256'), "
                    "('canonical_path', 'asset-relative-lowercase-slash-v1');"
                    "PRAGMA user_version=1;");
                transaction.Commit();
            });
        }
        IntegrityCheck();
        CatchDatabaseErrors([&]
        {
            auto statement = impl_->database->Prepare(
                "SELECT value FROM archive_meta WHERE key='hash_algorithm';");
            if (statement.Step() != database::StatementStep::Row ||
                statement.ColumnText(0) != "SHA-256")
            {
                throw ModelArchiveError(ModelArchiveErrorCode::InvalidDatabase,
                                        "archive hash algorithm metadata is missing or unsupported");
            }
        });
    }

    std::optional<SourceArchiveSnapshot> ModelArchiveDatabase::FindSource(
        std::string_view normalized_path)
    {
        const std::string path = NormalizeAssetRelativePath(normalized_path);
        return CatchDatabaseErrors([&]() -> std::optional<SourceArchiveSnapshot>
        {
            auto source_statement = impl_->database->Prepare(
                "SELECT id, normalized_path, path_hash, display_name, package_hash, "
                "importer_id, importer_version, settings_hash, native_model_version, "
                "status, diagnostic FROM sources WHERE normalized_path=?;");
            source_statement.Bind(1, path);
            if (source_statement.Step() != database::StatementStep::Row)
            {
                return std::nullopt;
            }

            SourceArchiveSnapshot snapshot;
            snapshot.source.id = source_statement.ColumnInt64(0);
            snapshot.source.normalized_path = source_statement.ColumnText(1);
            snapshot.source.path_hash = FromBlob(source_statement.ColumnBlob(2));
            snapshot.source.display_name = source_statement.ColumnText(3);
            snapshot.source.package_hash = FromBlob(source_statement.ColumnBlob(4));
            snapshot.source.importer_id = source_statement.ColumnText(5);
            snapshot.source.importer_version =
                static_cast<std::uint32_t>(source_statement.ColumnInt64(6));
            snapshot.source.settings_hash = FromBlob(source_statement.ColumnBlob(7));
            snapshot.source.native_model_version =
                static_cast<std::uint32_t>(source_statement.ColumnInt64(8));
            snapshot.source.status =
                static_cast<SourceImportStatus>(source_statement.ColumnInt64(9));
            snapshot.source.diagnostic = source_statement.ColumnText(10);

            auto dependency_statement = impl_->database->Prepare(
                "SELECT normalized_path, content_hash FROM source_dependencies "
                "WHERE source_id=? ORDER BY normalized_path;");
            dependency_statement.Bind(1, snapshot.source.id);
            while (dependency_statement.Step() == database::StatementStep::Row)
            {
                snapshot.dependencies.push_back(
                    {dependency_statement.ColumnText(0), FromBlob(dependency_statement.ColumnBlob(1))});
            }

            auto product_statement = impl_->database->Prepare(
                "SELECT DISTINCT p.content_hash, p.asset_type, p.relative_path, p.byte_size, "
                "p.schema_version FROM products p INNER JOIN source_products sp "
                "ON p.content_hash=sp.content_hash AND p.asset_type=sp.asset_type "
                "WHERE sp.source_id=? ORDER BY sp.role, sp.slot;");
            product_statement.Bind(1, snapshot.source.id);
            while (product_statement.Step() == database::StatementStep::Row)
            {
                snapshot.products.push_back(
                    {FromBlob(product_statement.ColumnBlob(0)),
                     static_cast<ArchiveProductType>(product_statement.ColumnInt64(1)),
                     product_statement.ColumnText(2),
                     static_cast<std::uint64_t>(product_statement.ColumnInt64(3)),
                     static_cast<std::uint32_t>(product_statement.ColumnInt64(4))});
            }

            auto source_product_statement = impl_->database->Prepare(
                "SELECT content_hash, asset_type, role, slot, display_name FROM source_products "
                "WHERE source_id=? ORDER BY role, slot;");
            source_product_statement.Bind(1, snapshot.source.id);
            while (source_product_statement.Step() == database::StatementStep::Row)
            {
                snapshot.source_products.push_back(
                    {FromBlob(source_product_statement.ColumnBlob(0)),
                     static_cast<ArchiveProductType>(source_product_statement.ColumnInt64(1)),
                     static_cast<std::int32_t>(source_product_statement.ColumnInt64(2)),
                     static_cast<std::int32_t>(source_product_statement.ColumnInt64(3)),
                     source_product_statement.ColumnText(4)});
            }

            auto override_statement = impl_->database->Prepare(
                "SELECT slot, authored_path FROM material_overrides WHERE source_id=? ORDER BY slot;");
            override_statement.Bind(1, snapshot.source.id);
            while (override_statement.Step() == database::StatementStep::Row)
            {
                snapshot.material_overrides.push_back(
                    {static_cast<std::int32_t>(override_statement.ColumnInt64(0)),
                     override_statement.ColumnText(1)});
            }
            return snapshot;
        });
    }

    std::optional<SourceArchiveSnapshot> ModelArchiveDatabase::FindSourceByLogicalPath(
        std::string_view logical_path)
    {
        const std::string normalized_logical_path = NormalizeAssetRelativePath(logical_path);
        return CatchDatabaseErrors([&]() -> std::optional<SourceArchiveSnapshot>
        {
            auto statement = impl_->database->Prepare(
                "SELECT normalized_path FROM sources ORDER BY normalized_path;");
            std::string matched_source_path;
            while (statement.Step() == database::StatementStep::Row)
            {
                const std::string source_path = statement.ColumnText(0);
                std::filesystem::path source_file{source_path};
                const std::string candidate = NormalizeAssetRelativePath(
                    source_file.replace_extension().generic_string());
                if (candidate != normalized_logical_path)
                {
                    continue;
                }
                if (!matched_source_path.empty())
                {
                    throw ModelArchiveError(
                        ModelArchiveErrorCode::InvalidDatabase,
                        "logical model path resolves to multiple source records: " +
                            normalized_logical_path);
                }
                matched_source_path = source_path;
            }
            if (matched_source_path.empty())
            {
                return std::nullopt;
            }
            return FindSource(matched_source_path);
        });
    }

    std::optional<std::filesystem::path> ModelArchiveDatabase::ResolveModelProductPath(
        std::string_view logical_path)
    {
        const std::optional<SourceArchiveSnapshot> snapshot =
            FindSourceByLogicalPath(logical_path);
        if (!snapshot.has_value())
        {
            return std::nullopt;
        }
        if (snapshot->source.status != SourceImportStatus::Ready)
        {
            throw ModelArchiveError(ModelArchiveErrorCode::SourceNotFound,
                                    "logical model source is not ready: " +
                                        std::string{logical_path});
        }

        for (const SourceProductRecord &source_product : snapshot->source_products)
        {
            if (source_product.asset_type != ArchiveProductType::Model)
            {
                continue;
            }
            const auto product = std::find_if(
                snapshot->products.begin(), snapshot->products.end(),
                [&source_product](const ProductRecord &candidate)
                {
                    return candidate.asset_type == ArchiveProductType::Model &&
                           candidate.content_hash == source_product.content_hash;
                });
            if (product == snapshot->products.end())
            {
                throw ModelArchiveError(ModelArchiveErrorCode::InvalidDatabase,
                                        "logical model source has no product metadata");
            }
            VerifyProductFile(*product);
            return ArchiveRoot() / product->relative_path;
        }
        throw ModelArchiveError(ModelArchiveErrorCode::InvalidDatabase,
                                "logical model source has no Model product");
    }

    void ModelArchiveDatabase::VerifyProductFile(const ProductRecord &product) const
    {
        ValidateHashPath(product);
        const std::filesystem::path path = ArchiveRoot() / product.relative_path;
        std::error_code error;
        const auto size = std::filesystem::file_size(path, error);
        if (error)
        {
            throw ModelArchiveError(ModelArchiveErrorCode::MissingProduct,
                                    "archive product is missing: " + path.string());
        }
        if (size != product.byte_size || Sha256File(path) != product.content_hash)
        {
            throw ModelArchiveError(ModelArchiveErrorCode::CorruptProduct,
                                    "archive product failed integrity verification: " +
                                        path.string());
        }
    }

    void ModelArchiveDatabase::ReplaceSource(
        const SourceRecord &source, const std::vector<SourceDependencyRecord> &dependencies,
        const std::vector<ProductRecord> &products,
        const std::vector<SourceProductRecord> &source_products,
        const std::vector<MaterialOverrideRecord> &material_overrides)
    {
        const std::string normalized_path = NormalizeAssetRelativePath(source.normalized_path);
        if (normalized_path != source.normalized_path || source.display_name.empty() ||
            source.importer_id.empty() || source.native_model_version == 0 ||
            source.path_hash != Sha256(source.normalized_path))
        {
            throw ModelArchiveError(ModelArchiveErrorCode::InvalidArgument,
                                    "source record is not canonical or is incomplete");
        }
        for (const SourceDependencyRecord &dependency : dependencies)
        {
            if (NormalizeAssetRelativePath(dependency.normalized_path) != dependency.normalized_path)
            {
                throw ModelArchiveError(ModelArchiveErrorCode::InvalidArgument,
                                        "source dependency path is not canonical");
            }
        }
        if (!dependencies.empty() &&
            source.package_hash !=
                HashSourcePackage([&dependencies]
                                  {
                                      std::vector<SourceFingerprintInput> inputs;
                                      inputs.reserve(dependencies.size());
                                      for (const SourceDependencyRecord &dependency : dependencies)
                                      {
                                          inputs.push_back({dependency.normalized_path,
                                                            dependency.content_hash});
                                      }
                                      return inputs;
                                  }()))
        {
            throw ModelArchiveError(ModelArchiveErrorCode::InvalidArgument,
                                    "source package hash does not match its dependencies");
        }
        for (const ProductRecord &product : products)
        {
            VerifyProductFile(product);
        }
        for (const SourceProductRecord &source_product : source_products)
        {
            const auto product = std::find_if(
                products.begin(), products.end(),
                [&source_product](const ProductRecord &candidate)
                {
                    return candidate.content_hash == source_product.content_hash &&
                           candidate.asset_type == source_product.asset_type;
                });
            if (product == products.end())
            {
                throw ModelArchiveError(ModelArchiveErrorCode::InvalidArgument,
                                        "source product reference has no published product");
            }
        }
        for (const MaterialOverrideRecord &override_record : material_overrides)
        {
            if (override_record.slot < 0)
            {
                throw ModelArchiveError(ModelArchiveErrorCode::InvalidArgument,
                                        "material override slot cannot be negative");
            }
            (void)NormalizeAssetRelativePath(override_record.authored_path);
        }

        CatchDatabaseErrors([&]
        {
            database::Transaction transaction{*impl_->database};
            auto product_insert = impl_->database->Prepare(
                "INSERT INTO products(content_hash, asset_type, relative_path, byte_size, schema_version) "
                "VALUES(?,?,?,?,?) ON CONFLICT(content_hash, asset_type) DO NOTHING;");
            auto product_select = impl_->database->Prepare(
                "SELECT relative_path, byte_size, schema_version FROM products "
                "WHERE content_hash=? AND asset_type=?;");
            for (const ProductRecord &product : products)
            {
                const std::vector<std::byte> hash = ToBlob(product.content_hash);
                product_insert.Bind(1, hash);
                product_insert.Bind(2, static_cast<std::int64_t>(product.asset_type));
                product_insert.Bind(3, product.relative_path);
                product_insert.Bind(4, static_cast<std::int64_t>(product.byte_size));
                product_insert.Bind(5, static_cast<std::int64_t>(product.schema_version));
                (void)product_insert.Step();
                product_insert.Reset();
                product_insert.ClearBindings();

                product_select.Bind(1, hash);
                product_select.Bind(2, static_cast<std::int64_t>(product.asset_type));
                if (product_select.Step() != database::StatementStep::Row ||
                    product_select.ColumnText(0) != product.relative_path ||
                    static_cast<std::uint64_t>(product_select.ColumnInt64(1)) != product.byte_size ||
                    static_cast<std::uint32_t>(product_select.ColumnInt64(2)) !=
                        product.schema_version)
                {
                    throw ModelArchiveError(ModelArchiveErrorCode::CorruptProduct,
                                            "immutable archive product metadata changed");
                }
                product_select.Reset();
                product_select.ClearBindings();
            }

            auto source_id_statement = impl_->database->Prepare(
                "SELECT id FROM sources WHERE normalized_path=?;");
            source_id_statement.Bind(1, source.normalized_path);
            std::int64_t source_id{};
            if (source_id_statement.Step() == database::StatementStep::Row)
            {
                source_id = source_id_statement.ColumnInt64(0);
                auto update = impl_->database->Prepare(
                    "UPDATE sources SET path_hash=?, display_name=?, package_hash=?, importer_id=?, "
                    "importer_version=?, settings_hash=?, native_model_version=?, status=?, diagnostic=? "
                    "WHERE id=?;");
                update.Bind(1, ToBlob(source.path_hash));
                update.Bind(2, source.display_name);
                update.Bind(3, ToBlob(source.package_hash));
                update.Bind(4, source.importer_id);
                update.Bind(5, static_cast<std::int64_t>(source.importer_version));
                update.Bind(6, ToBlob(source.settings_hash));
                update.Bind(7, static_cast<std::int64_t>(source.native_model_version));
                update.Bind(8, static_cast<std::int64_t>(source.status));
                update.Bind(9, source.diagnostic);
                update.Bind(10, source_id);
                (void)update.Step();
            }
            else
            {
                auto insert = impl_->database->Prepare(
                    "INSERT INTO sources(normalized_path, path_hash, display_name, package_hash, "
                    "importer_id, importer_version, settings_hash, native_model_version, status, diagnostic) "
                    "VALUES(?,?,?,?,?,?,?,?,?,?);");
                insert.Bind(1, source.normalized_path);
                insert.Bind(2, ToBlob(source.path_hash));
                insert.Bind(3, source.display_name);
                insert.Bind(4, ToBlob(source.package_hash));
                insert.Bind(5, source.importer_id);
                insert.Bind(6, static_cast<std::int64_t>(source.importer_version));
                insert.Bind(7, ToBlob(source.settings_hash));
                insert.Bind(8, static_cast<std::int64_t>(source.native_model_version));
                insert.Bind(9, static_cast<std::int64_t>(source.status));
                insert.Bind(10, source.diagnostic);
                (void)insert.Step();
                source_id = impl_->database->LastInsertRowID();
            }

            for (const char *table : {"source_dependencies", "source_products", "material_overrides"})
            {
                auto delete_statement = impl_->database->Prepare(
                    std::string{"DELETE FROM "} + table + " WHERE source_id=?;");
                delete_statement.Bind(1, source_id);
                (void)delete_statement.Step();
            }

            auto dependency_insert = impl_->database->Prepare(
                "INSERT INTO source_dependencies(source_id, normalized_path, content_hash) VALUES(?,?,?);");
            for (const SourceDependencyRecord &dependency : dependencies)
            {
                dependency_insert.Bind(1, source_id);
                dependency_insert.Bind(2, dependency.normalized_path);
                dependency_insert.Bind(3, ToBlob(dependency.content_hash));
                (void)dependency_insert.Step();
                dependency_insert.Reset();
                dependency_insert.ClearBindings();
            }

            auto product_reference_insert = impl_->database->Prepare(
                "INSERT INTO source_products(source_id, content_hash, asset_type, role, slot, display_name) "
                "VALUES(?,?,?,?,?,?);");
            for (const SourceProductRecord &product : source_products)
            {
                product_reference_insert.Bind(1, source_id);
                product_reference_insert.Bind(2, ToBlob(product.content_hash));
                product_reference_insert.Bind(3, static_cast<std::int64_t>(product.asset_type));
                product_reference_insert.Bind(4, static_cast<std::int64_t>(product.role));
                product_reference_insert.Bind(5, static_cast<std::int64_t>(product.slot));
                product_reference_insert.Bind(6, product.display_name);
                (void)product_reference_insert.Step();
                product_reference_insert.Reset();
                product_reference_insert.ClearBindings();
            }

            auto override_insert = impl_->database->Prepare(
                "INSERT INTO material_overrides(source_id, slot, authored_path) VALUES(?,?,?);");
            for (const MaterialOverrideRecord &override_record : material_overrides)
            {
                override_insert.Bind(1, source_id);
                override_insert.Bind(2, static_cast<std::int64_t>(override_record.slot));
                override_insert.Bind(3, override_record.authored_path);
                (void)override_insert.Step();
                override_insert.Reset();
                override_insert.ClearBindings();
            }
            transaction.Commit();
        });
    }

    ArchiveProbeResult ModelArchiveDatabase::ProbeSource(const SourceProbeRequest &request)
    {
        ArchiveProbeResult result;
        result.snapshot = FindSource(request.normalized_path);
        if (!result.snapshot.has_value())
        {
            result.status = ArchiveProbeStatus::SourceNotFound;
            result.diagnostic = ProbeDiagnostic(result.status);
            return result;
        }
        const SourceRecord &source = result.snapshot->source;
        if (source.status != SourceImportStatus::Ready)
        {
            result.status = ArchiveProbeStatus::SourceFailed;
        }
        else if (source.package_hash != request.package_hash)
        {
            result.status = ArchiveProbeStatus::SourcePackageChanged;
        }
        else if (source.importer_id != request.importer_id ||
                 source.importer_version != request.importer_version)
        {
            result.status = ArchiveProbeStatus::ImporterChanged;
        }
        else if (source.settings_hash != request.settings_hash)
        {
            result.status = ArchiveProbeStatus::SettingsChanged;
        }
        else if (source.native_model_version != request.native_model_version)
        {
            result.status = ArchiveProbeStatus::NativeSchemaChanged;
        }
        else
        {
            for (const ProductRecord &product : result.snapshot->products)
            {
                try
                {
                    VerifyProductFile(product);
                }
                catch (const ModelArchiveError &error)
                {
                    result.status = error.Code() == ModelArchiveErrorCode::MissingProduct
                                        ? ArchiveProbeStatus::MissingProduct
                                        : ArchiveProbeStatus::CorruptProduct;
                    result.diagnostic = error.what();
                    return result;
                }
            }
            result.status = ArchiveProbeStatus::UpToDate;
        }
        result.diagnostic = ProbeDiagnostic(result.status);
        return result;
    }

    void ModelArchiveDatabase::IntegrityCheck()
    {
        CatchDatabaseErrors([&]
        {
            auto statement = impl_->database->Prepare("PRAGMA quick_check;");
            if (statement.Step() != database::StatementStep::Row ||
                statement.ColumnText(0) != "ok")
            {
                throw ModelArchiveError(ModelArchiveErrorCode::InvalidDatabase,
                                        "archive database quick_check failed");
            }
        });
    }
#endif
}

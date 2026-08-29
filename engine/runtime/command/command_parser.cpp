#include "command/command_parser.h"

#include <algorithm>
#include <charconv>
#include <cerrno>
#include <cmath>
#include <cstdlib>
#include <set>
#include <sstream>
#include <type_traits>
#include <utility>

namespace kpengine::runtime::command
{
    namespace
    {
        struct LexToken
        {
            std::string value;
        };

        bool IsWhitespace(const char character) noexcept
        {
            return character == ' ' || character == '\t' || character == '\n' ||
                   character == '\r' || character == '\f' || character == '\v';
        }

        std::string ExpectedTypeName(const CommandValueType type)
        {
            switch (type)
            {
            case CommandValueType::Boolean:
                return "boolean";
            case CommandValueType::SignedInteger:
                return "signed integer";
            case CommandValueType::UnsignedInteger:
                return "unsigned integer";
            case CommandValueType::Float:
                return "float";
            case CommandValueType::String:
                return "string";
            case CommandValueType::Enum:
                return "enum";
            }
            return "value";
        }

        bool IsValueType(const CommandValue &value, const CommandValueType type)
        {
            switch (type)
            {
            case CommandValueType::Boolean:
                return std::holds_alternative<bool>(value);
            case CommandValueType::SignedInteger:
                return std::holds_alternative<int64_t>(value);
            case CommandValueType::UnsignedInteger:
                return std::holds_alternative<uint64_t>(value);
            case CommandValueType::Float:
                return std::holds_alternative<double>(value);
            case CommandValueType::String:
            case CommandValueType::Enum:
                return std::holds_alternative<std::string>(value);
            }
            return false;
        }

        bool IsEnumValue(const std::string &value,
                         const std::vector<std::string> &allowed_values)
        {
            return std::find(allowed_values.begin(), allowed_values.end(), value) !=
                   allowed_values.end();
        }

        bool ParseBoolean(const std::string &text, bool &value)
        {
            if (text == "true" || text == "1")
            {
                value = true;
                return true;
            }
            if (text == "false" || text == "0")
            {
                value = false;
                return true;
            }
            return false;
        }

        bool ParseSignedInteger(const std::string &text, int64_t &value)
        {
            if (text.empty())
            {
                return false;
            }
            const char *const begin = text.data();
            const char *const end = begin + text.size();
            const auto result = std::from_chars(begin, end, value, 10);
            return result.ec == std::errc{} && result.ptr == end;
        }

        bool ParseUnsignedInteger(const std::string &text, uint64_t &value)
        {
            if (text.empty() || text.front() == '-')
            {
                return false;
            }
            const char *const begin = text.data();
            const char *const end = begin + text.size();
            const auto result = std::from_chars(begin, end, value, 10);
            return result.ec == std::errc{} && result.ptr == end;
        }

        bool ParseFloat(const std::string &text, double &value)
        {
            if (text.empty())
            {
                return false;
            }
            char *end = nullptr;
            errno = 0;
            value = std::strtod(text.c_str(), &end);
            return errno != ERANGE && end == text.c_str() + text.size() &&
                   std::isfinite(value);
        }

        std::optional<CommandValue> ParseTextValue(const std::string &text,
                                                    const CommandArgumentDesc &argument,
                                                    std::string &diagnostic)
        {
            switch (argument.type)
            {
            case CommandValueType::Boolean:
            {
                bool value = false;
                if (ParseBoolean(text, value))
                {
                    return value;
                }
                break;
            }
            case CommandValueType::SignedInteger:
            {
                int64_t value = 0;
                if (ParseSignedInteger(text, value))
                {
                    return value;
                }
                break;
            }
            case CommandValueType::UnsignedInteger:
            {
                uint64_t value = 0;
                if (ParseUnsignedInteger(text, value))
                {
                    return value;
                }
                break;
            }
            case CommandValueType::Float:
            {
                double value = 0.0;
                if (ParseFloat(text, value))
                {
                    return value;
                }
                break;
            }
            case CommandValueType::String:
                return text;
            case CommandValueType::Enum:
                if (IsEnumValue(text, argument.enum_values))
                {
                    return text;
                }
                diagnostic = "argument '" + argument.name + "': invalid enum value '" +
                             text + "'";
                return std::nullopt;
            }

            diagnostic = "argument '" + argument.name + "': invalid " +
                         ExpectedTypeName(argument.type) + " '" + text + "'";
            return std::nullopt;
        }

        bool Tokenize(std::string_view text,
                      std::vector<LexToken> &tokens,
                      std::string &diagnostic,
                      const bool allow_incomplete)
        {
            std::string current;
            bool token_started = false;
            bool escaped = false;
            char quote = '\0';

            const auto push_token = [&]
            {
                if (token_started)
                {
                    tokens.push_back({std::move(current)});
                    current.clear();
                    token_started = false;
                }
            };

            for (const char character : text)
            {
                if (escaped)
                {
                    switch (character)
                    {
                    case 'n':
                        current.push_back('\n');
                        break;
                    case 'r':
                        current.push_back('\r');
                        break;
                    case 't':
                        current.push_back('\t');
                        break;
                    default:
                        current.push_back(character);
                        break;
                    }
                    escaped = false;
                    token_started = true;
                    continue;
                }

                if (character == '\\')
                {
                    escaped = true;
                    token_started = true;
                    continue;
                }

                if (quote != '\0')
                {
                    if (character == quote)
                    {
                        quote = '\0';
                    }
                    else
                    {
                        current.push_back(character);
                    }
                    token_started = true;
                    continue;
                }

                if (character == '\'' || character == '"')
                {
                    quote = character;
                    token_started = true;
                }
                else if (IsWhitespace(character))
                {
                    push_token();
                }
                else
                {
                    current.push_back(character);
                    token_started = true;
                }
            }

            if (escaped && !allow_incomplete)
            {
                diagnostic = "trailing escape character";
                return false;
            }
            if (quote != '\0' && !allow_incomplete)
            {
                diagnostic = "unterminated quoted argument";
                return false;
            }
            push_token();
            return true;
        }

        const CommandArgumentDesc *FindArgument(const CommandSchema &schema,
                                                 const std::string &name)
        {
            const auto iterator = std::find_if(
                schema.arguments.begin(), schema.arguments.end(),
                [&name](const CommandArgumentDesc &argument) { return argument.name == name; });
            return iterator == schema.arguments.end() ? nullptr : &*iterator;
        }

        std::string SchemaDiagnostic(const std::string &argument_name,
                                     const std::string &reason)
        {
            return "schema argument '" + argument_name + "': " + reason;
        }

        std::string TokenArgumentName(const std::string &token)
        {
            const size_t equals = token.find('=');
            return equals == std::string::npos ? token : token.substr(0, equals);
        }

        void AddCompletion(std::set<std::string> &candidates, const std::string &candidate,
                           const std::string &prefix)
        {
            if (candidate.rfind(prefix, 0) == 0)
            {
                candidates.insert(candidate);
            }
        }
    }

    bool CommandParser::ValidateSchema(const CommandSchema &schema,
                                       std::string &diagnostic)
    {
        std::set<std::string> names;
        for (const CommandArgumentDesc &argument : schema.arguments)
        {
            if (argument.name.empty())
            {
                diagnostic = "schema argument name must not be empty";
                return false;
            }
            if (!names.insert(argument.name).second)
            {
                diagnostic = SchemaDiagnostic(argument.name, "duplicate argument name");
                return false;
            }
            if (argument.type == CommandValueType::Enum && argument.enum_values.empty())
            {
                diagnostic = SchemaDiagnostic(argument.name, "enum values are required");
                return false;
            }
            if (argument.type != CommandValueType::Enum && !argument.enum_values.empty())
            {
                diagnostic = SchemaDiagnostic(argument.name,
                                              "enum values require enum type");
                return false;
            }
            if (argument.required && !std::holds_alternative<std::monostate>(argument.default_value))
            {
                diagnostic = SchemaDiagnostic(argument.name,
                                              "required arguments cannot have defaults");
                return false;
            }
            if (!std::holds_alternative<std::monostate>(argument.default_value) &&
                !IsValueType(argument.default_value, argument.type))
            {
                diagnostic = SchemaDiagnostic(argument.name,
                                              "default value has the wrong type");
                return false;
            }
            if (argument.type == CommandValueType::Enum &&
                !std::holds_alternative<std::monostate>(argument.default_value) &&
                !IsEnumValue(std::get<std::string>(argument.default_value),
                             argument.enum_values))
            {
                diagnostic = SchemaDiagnostic(argument.name,
                                              "default value is not an allowed enum value");
                return false;
            }
        }
        diagnostic.clear();
        return true;
    }

    std::string CommandParser::FormatHelp(const CommandDesc &descriptor)
    {
        std::ostringstream output;
        output << descriptor.name;
        if (!descriptor.help.empty())
        {
            output << " - " << descriptor.help;
        }
        output << "\nUsage: " << descriptor.name;
        for (const CommandArgumentDesc &argument : descriptor.schema.arguments)
        {
            output << (argument.required ? " " : " [") << argument.name;
            if (!argument.required)
            {
                output << "]";
            }
        }
        if (!descriptor.schema.arguments.empty())
        {
            output << "\nArguments:";
            for (const CommandArgumentDesc &argument : descriptor.schema.arguments)
            {
                output << "\n  " << argument.name << ": " << ExpectedTypeName(argument.type);
                if (argument.required)
                {
                    output << ", required";
                }
                if (argument.type == CommandValueType::Enum)
                {
                    output << ", values=";
                    for (size_t index = 0; index < argument.enum_values.size(); ++index)
                    {
                        if (index != 0)
                        {
                            output << '|';
                        }
                        output << argument.enum_values[index];
                    }
                }
                if (!std::holds_alternative<std::monostate>(argument.default_value))
                {
                    output << ", default=";
                    std::visit([&output](const auto &value)
                    {
                        using ValueType = std::decay_t<decltype(value)>;
                        if constexpr (!std::is_same_v<ValueType, std::monostate>)
                        {
                            if constexpr (std::is_same_v<ValueType, bool>)
                            {
                                output << (value ? "true" : "false");
                            }
                            else
                            {
                                output << value;
                            }
                        }
                    }, argument.default_value);
                }
            }
        }
        return output.str();
    }

    CommandParseResult CommandParser::Validate(const CommandCall &call,
                                               const CommandDesc &descriptor)
    {
        if (call.name != descriptor.name)
        {
            return {{}, "command name does not match descriptor"};
        }

        std::string schema_diagnostic;
        if (!ValidateSchema(descriptor.schema, schema_diagnostic))
        {
            return {{}, schema_diagnostic};
        }

        CommandCall normalized{descriptor.name, {}};
        for (const auto &provided : call.arguments)
        {
            if (FindArgument(descriptor.schema, provided.first) == nullptr)
            {
                return {{}, "argument '" + provided.first + "': unknown argument"};
            }
        }

        for (const CommandArgumentDesc &argument : descriptor.schema.arguments)
        {
            const auto provided = call.arguments.find(argument.name);
            if (provided == call.arguments.end())
            {
                if (!std::holds_alternative<std::monostate>(argument.default_value))
                {
                    normalized.arguments.emplace(argument.name, argument.default_value);
                }
                else if (argument.required)
                {
                    return {{}, "argument '" + argument.name + "': required argument is missing"};
                }
                continue;
            }

            if (!IsValueType(provided->second, argument.type))
            {
                return {{}, "argument '" + argument.name + "': expected " +
                                ExpectedTypeName(argument.type)};
            }
            if (argument.type == CommandValueType::Enum &&
                !IsEnumValue(std::get<std::string>(provided->second), argument.enum_values))
            {
                return {{}, "argument '" + argument.name + "': invalid enum value '" +
                                std::get<std::string>(provided->second) + "'"};
            }
            normalized.arguments.emplace(argument.name, provided->second);
        }

        return {std::move(normalized), {}};
    }

    CommandParseResult CommandParser::Parse(std::string_view text,
                                            const CommandDesc &descriptor)
    {
        std::vector<LexToken> tokens;
        std::string diagnostic;
        if (!Tokenize(text, tokens, diagnostic, false))
        {
            return {{}, diagnostic};
        }
        if (tokens.empty())
        {
            return {{}, "command name is required"};
        }
        if (tokens.front().value != descriptor.name)
        {
            return {{}, "command name does not match descriptor"};
        }

        CommandArguments raw_arguments;
        std::vector<std::string> positional;
        for (size_t index = 1; index < tokens.size(); ++index)
        {
            const std::string &token = tokens[index].value;
            const size_t equals = token.find('=');
            if (equals == std::string::npos)
            {
                positional.push_back(token);
                continue;
            }
            const std::string name = token.substr(0, equals);
            if (name.empty())
            {
                return {{}, "argument name must not be empty"};
            }
            if (raw_arguments.find(name) != raw_arguments.end())
            {
                return {{}, "argument '" + name + "': duplicate argument"};
            }
            const CommandArgumentDesc *const argument = FindArgument(descriptor.schema, name);
            if (argument == nullptr)
            {
                return {{}, "argument '" + name + "': unknown argument"};
            }
            const std::string value = token.substr(equals + 1);
            const auto parsed = ParseTextValue(value, *argument, diagnostic);
            if (!parsed.has_value())
            {
                return {{}, diagnostic};
            }
            raw_arguments.emplace(name, *parsed);
        }

        size_t argument_index = 0;
        for (const std::string &value : positional)
        {
            while (argument_index < descriptor.schema.arguments.size() &&
                   raw_arguments.find(descriptor.schema.arguments[argument_index].name) !=
                       raw_arguments.end())
            {
                ++argument_index;
            }
            if (argument_index == descriptor.schema.arguments.size())
            {
                return {{}, "too many positional arguments"};
            }
            const CommandArgumentDesc &argument = descriptor.schema.arguments[argument_index++];
            const auto parsed = ParseTextValue(value, argument, diagnostic);
            if (!parsed.has_value())
            {
                return {{}, diagnostic};
            }
            raw_arguments.emplace(argument.name, *parsed);
        }

        return Validate({descriptor.name, std::move(raw_arguments)}, descriptor);
    }

    std::optional<std::string> CommandParser::ExtractCommandName(std::string_view text,
                                                                 std::string &diagnostic)
    {
        std::vector<LexToken> tokens;
        if (!Tokenize(text, tokens, diagnostic, false))
        {
            return std::nullopt;
        }
        if (tokens.empty())
        {
            diagnostic = "command name is required";
            return std::nullopt;
        }
        diagnostic.clear();
        return tokens.front().value;
    }

    std::vector<std::string> CommandParser::Complete(
        std::string_view text,
        const std::vector<CommandDesc> &descriptors)
    {
        std::vector<LexToken> tokens;
        std::string diagnostic;
        if (!Tokenize(text, tokens, diagnostic, true))
        {
            return {};
        }

        std::set<std::string> candidates;
        const bool trailing_space = !text.empty() && IsWhitespace(text.back());
        if (tokens.empty() || (tokens.size() == 1 && !trailing_space))
        {
            const std::string prefix = tokens.empty() ? std::string{} : tokens.front().value;
            for (const CommandDesc &descriptor : descriptors)
            {
                AddCompletion(candidates, descriptor.name, prefix);
            }
            return {candidates.begin(), candidates.end()};
        }

        const std::string command_name = tokens.front().value;
        const auto descriptor_iterator = std::find_if(
            descriptors.begin(), descriptors.end(),
            [&command_name](const CommandDesc &descriptor)
            { return descriptor.name == command_name; });
        if (descriptor_iterator == descriptors.end())
        {
            return {};
        }

        std::set<std::string> used_names;
        for (size_t index = 1; index + (trailing_space ? 0 : 1) < tokens.size(); ++index)
        {
            const std::string name = TokenArgumentName(tokens[index].value);
            if (tokens[index].value.find('=') != std::string::npos)
            {
                used_names.insert(name);
            }
        }

        const std::string current = trailing_space ? std::string{} : tokens.back().value;
        const size_t equals = current.find('=');
        if (equals != std::string::npos)
        {
            const std::string name = current.substr(0, equals);
            const CommandArgumentDesc *const argument = FindArgument(descriptor_iterator->schema,
                                                                       name);
            if (argument != nullptr && argument->type == CommandValueType::Enum)
            {
                for (const std::string &value : argument->enum_values)
                {
                    AddCompletion(candidates, name + "=" + value, current);
                }
            }
            return {candidates.begin(), candidates.end()};
        }

        size_t positional_index = 0;
        for (size_t index = 1; index < tokens.size() - (trailing_space ? 0 : 1); ++index)
        {
            if (tokens[index].value.find('=') == std::string::npos)
            {
                while (positional_index < descriptor_iterator->schema.arguments.size() &&
                       used_names.find(descriptor_iterator->schema.arguments[positional_index].name) !=
                           used_names.end())
                {
                    ++positional_index;
                }
                ++positional_index;
            }
        }
        while (positional_index < descriptor_iterator->schema.arguments.size() &&
               used_names.find(descriptor_iterator->schema.arguments[positional_index].name) !=
                   used_names.end())
        {
            ++positional_index;
        }
        if (positional_index < descriptor_iterator->schema.arguments.size())
        {
            const CommandArgumentDesc &argument = descriptor_iterator->schema.arguments[positional_index];
            if (argument.type == CommandValueType::Enum)
            {
                for (const std::string &value : argument.enum_values)
                {
                    AddCompletion(candidates, value, current);
                }
            }
        }

        for (const CommandArgumentDesc &argument : descriptor_iterator->schema.arguments)
        {
            if (used_names.find(argument.name) == used_names.end())
            {
                AddCompletion(candidates, argument.name + "=", current);
            }
        }
        return {candidates.begin(), candidates.end()};
    }
}

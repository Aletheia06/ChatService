#include "common/Json.h"

#include <cctype>
#include <sstream>

namespace chatservice
{
namespace
{

void skipWhitespace(const std::string& text, std::size_t* pos)
{
  while (*pos < text.size() &&
         std::isspace(static_cast<unsigned char>(text[*pos])) != 0)
  {
    ++(*pos);
  }
}

bool hexValue(char ch, int* value)
{
  if (ch >= '0' && ch <= '9')
  {
    *value = ch - '0';
    return true;
  }
  if (ch >= 'a' && ch <= 'f')
  {
    *value = ch - 'a' + 10;
    return true;
  }
  if (ch >= 'A' && ch <= 'F')
  {
    *value = ch - 'A' + 10;
    return true;
  }
  return false;
}

bool parseUnicodeEscape(const std::string& text,
                        std::size_t* pos,
                        std::string* output,
                        std::string* error)
{
  if (*pos + 4 > text.size())
  {
    *error = "incomplete unicode escape";
    return false;
  }

  int code = 0;
  for (int i = 0; i < 4; ++i)
  {
    int digit = 0;
    if (!hexValue(text[*pos + static_cast<std::size_t>(i)], &digit))
    {
      *error = "invalid unicode escape";
      return false;
    }
    code = (code << 4) + digit;
  }

  *pos += 4;
  if (code > 0x7f)
  {
    *error = "unicode escapes above ASCII are not supported";
    return false;
  }

  output->push_back(static_cast<char>(code));
  return true;
}

bool parseJsonString(const std::string& text,
                     std::size_t* pos,
                     std::string* output,
                     std::string* error)
{
  if (*pos >= text.size() || text[*pos] != '"')
  {
    *error = "expected string";
    return false;
  }

  ++(*pos);
  output->clear();

  while (*pos < text.size())
  {
    const char ch = text[*pos];
    ++(*pos);

    if (ch == '"')
    {
      return true;
    }

    if (ch == '\\')
    {
      if (*pos >= text.size())
      {
        *error = "incomplete escape sequence";
        return false;
      }

      const char escaped = text[*pos];
      ++(*pos);

      switch (escaped)
      {
        case '"':
        case '\\':
        case '/':
          output->push_back(escaped);
          break;
        case 'b':
          output->push_back('\b');
          break;
        case 'f':
          output->push_back('\f');
          break;
        case 'n':
          output->push_back('\n');
          break;
        case 'r':
          output->push_back('\r');
          break;
        case 't':
          output->push_back('\t');
          break;
        case 'u':
          if (!parseUnicodeEscape(text, pos, output, error))
          {
            return false;
          }
          break;
        default:
          *error = "invalid escape sequence";
          return false;
      }
    }
    else
    {
      if (static_cast<unsigned char>(ch) < 0x20)
      {
        *error = "control character in string";
        return false;
      }
      output->push_back(ch);
    }
  }

  *error = "unterminated string";
  return false;
}

}  // namespace

bool parseJsonObject(const std::string& text,
                     JsonObject* object,
                     std::string* error)
{
  object->clear();
  std::size_t pos = 0;
  skipWhitespace(text, &pos);

  if (pos >= text.size() || text[pos] != '{')
  {
    *error = "expected object";
    return false;
  }
  ++pos;
  skipWhitespace(text, &pos);

  if (pos < text.size() && text[pos] == '}')
  {
    ++pos;
    skipWhitespace(text, &pos);
    if (pos != text.size())
    {
      *error = "unexpected content after object";
      return false;
    }
    return true;
  }

  while (pos < text.size())
  {
    std::string key;
    std::string value;
    if (!parseJsonString(text, &pos, &key, error))
    {
      return false;
    }

    skipWhitespace(text, &pos);
    if (pos >= text.size() || text[pos] != ':')
    {
      *error = "expected colon";
      return false;
    }
    ++pos;
    skipWhitespace(text, &pos);

    if (!parseJsonString(text, &pos, &value, error))
    {
      return false;
    }
    (*object)[key] = value;

    skipWhitespace(text, &pos);
    if (pos >= text.size())
    {
      *error = "unterminated object";
      return false;
    }
    if (text[pos] == '}')
    {
      ++pos;
      skipWhitespace(text, &pos);
      if (pos != text.size())
      {
        *error = "unexpected content after object";
        return false;
      }
      return true;
    }
    if (text[pos] != ',')
    {
      *error = "expected comma";
      return false;
    }
    ++pos;
    skipWhitespace(text, &pos);
  }

  *error = "unterminated object";
  return false;
}

std::string escapeJsonString(const std::string& value)
{
  std::string output;
  const char* hex = "0123456789abcdef";

  for (std::string::const_iterator it = value.begin(); it != value.end(); ++it)
  {
    const char ch = *it;
    switch (ch)
    {
      case '"':
        output += "\\\"";
        break;
      case '\\':
        output += "\\\\";
        break;
      case '\b':
        output += "\\b";
        break;
      case '\f':
        output += "\\f";
        break;
      case '\n':
        output += "\\n";
        break;
      case '\r':
        output += "\\r";
        break;
      case '\t':
        output += "\\t";
        break;
      default:
      {
        const unsigned char uch = static_cast<unsigned char>(ch);
        if (uch < 0x20)
        {
          output += "\\u00";
          output.push_back(hex[uch >> 4]);
          output.push_back(hex[uch & 0x0f]);
        }
        else
        {
          output.push_back(ch);
        }
        break;
      }
    }
  }

  return output;
}

std::string serializeJsonObject(const JsonObject& object)
{
  std::ostringstream stream;
  stream << "{";
  bool first = true;
  for (JsonObject::const_iterator it = object.begin(); it != object.end(); ++it)
  {
    if (!first)
    {
      stream << ",";
    }
    first = false;
    stream << "\"" << escapeJsonString(it->first) << "\":"
           << "\"" << escapeJsonString(it->second) << "\"";
  }
  stream << "}";
  return stream.str();
}

std::string makeJsonLine(const JsonObject& object)
{
  return serializeJsonObject(object) + "\n";
}

}  // namespace chatservice

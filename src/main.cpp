#include <sys/wait.h>
#include <unistd.h>
#include <fcntl.h>

#include <algorithm>
#include <cctype>
#include <cerrno>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <iostream>
#include <optional>
#include <string>
#include <unordered_set>
#include <vector>

#ifdef _WIN32
constexpr char PATH_LIST_SEPARATOR[] = ";";
#else
constexpr char PATH_LIST_SEPARATOR[] = ":";
#endif

struct userInput {
  std::string raw_input; // user's raw input
  int redirect_position; // where the > is in the input
  int redirect_fd; // file descriptor to be acted on
  std::vector<std::string> parsed_input; // raw input parsed into a vector of strings
};

const std::unordered_set<std::string> builtins = {"echo", "exit", "type", "pwd", "cd"};

// Number of tokens in parsed_input that are actually command/args, excluding
// the '>' and its target when a redirect is present.
size_t effective_arg_count(const userInput& input) {
  return input.redirect_position >= 0 ? static_cast<size_t>(input.redirect_position)
                                       : input.parsed_input.size();
}

// Rejects a redirect with no command before it or no filename after it,
// reporting an error. Every later access of parsed_input[redirect_position +
// 1] or command_args[0] relies on this having been checked first.
bool validate_redirect_syntax(const userInput& input) {
  if (input.redirect_position < 0) {
    return true;
  }
  if (input.redirect_position == 0) {
    std::cerr << "syntax error: unexpected token '>'" << std::endl;
    return false;
  }
  if (static_cast<size_t>(input.redirect_position) + 1 >= input.parsed_input.size()) {
    std::cerr << "syntax error: expected a filename after '>'" << std::endl;
    return false;
  }
  return true;
}

std::string is_in_path(const std::string& command, const std::vector<std::string>& path) {
  for (auto const& p : path) {
    std::string file = p + "/" + command;
    if (!access(file.c_str(), X_OK)) {
      return file;
    }
  }
  return "";
}

// delim is treated as a set of delimiter characters (like shell IFS), not a literal substring.
std::vector<std::string> parse_string(userInput& user_input, const std::string& delim) {
  std::vector<std::string> result;
  std::string token;
  bool in_single_quotes = false;
  bool in_double_quotes = false;
  bool token_started = false;
  const std::string s = user_input.raw_input;
  for (size_t i = 0; i < s.size(); i++) {
    char c = s[i];
    if ((!in_single_quotes && !in_double_quotes) && delim.find(c) != std::string::npos) {
      if (token_started) {
        result.push_back(token);
        token.clear();
        token_started = false;
      }
      continue;
    }
    if (c == '>' && !in_single_quotes && !in_double_quotes) {
      user_input.redirect_fd = 1;
      if (token_started) {
        bool all_digits =
            !token.empty() && std::all_of(token.begin(), token.end(),
                                          [](unsigned char ch) { return std::isdigit(ch); });
        if (all_digits) {
          user_input.redirect_fd = std::stoi(token);
        } else {
          result.push_back(token);
        }
        token.clear();
        token_started = false;
      }
      user_input.redirect_position = static_cast<int>(result.size());
      result.push_back(">");
      continue;
    }
    if (c == '\\' && !in_double_quotes && !in_single_quotes) {
      if (i + 1 == s.size()) {
        std::cerr << "unmatched \\" << std::endl;
        return {};
      }
      token += s[++i];
      token_started = true;
      continue;
    }
    if (c == '\\' && in_double_quotes) {  // escape certain chars in ""
      if (i + 1 == s.size()) {
        std::cerr << "unmatched \"" << std::endl;
        return {};
      }
      if (s[i + 1] == '\"' || s[i + 1] == '\\') {
        token += s[++i];
        token_started = true;
        continue;
      }
    }
    if (c == '\'' && !in_double_quotes) {
      in_single_quotes = !in_single_quotes;
      token_started = true;
      continue;
    }
    if (c == '\"' && !in_single_quotes) {
      in_double_quotes = !in_double_quotes;
      token_started = true;
      continue;
    }
    token += c;
    token_started = true;
  }
  if (in_single_quotes || in_double_quotes) {
    std::cerr << "unmatched " << (in_single_quotes ? "'" : "\"") << std::endl;
    return {};
  }
  if (token_started) {
    result.push_back(token);
  }
  return result;
}

std::vector<std::string> parse_path(const std::string& s, const std::string& delim) {
  std::vector<std::string> result;
  size_t start = 0, end;
  while ((end = s.find(delim, start)) != std::string::npos) {
    std::string token = s.substr(start, end - start);
    if (!token.empty()) {
      result.push_back(token);
    }
    start = end + delim.size();
  }
  std::string last = s.substr(start);
  if (!last.empty()) {
    result.push_back(last);
  }
  return result;
}

void builtin_echo(userInput& input) {
  size_t n = effective_arg_count(input);
  for (size_t i = 1; i < n; i++) {
    if (i > 1)
      std::cout << " ";
    std::cout << input.parsed_input[i];
  }
  std::cout << std::endl;
}

void builtin_type(userInput& input, const std::vector<std::string>& path) {
  if (effective_arg_count(input) < 2) {
    std::cerr << "type: missing argument" << std::endl;
    return;
  }
  const std::string& target = input.parsed_input[1];
  if (builtins.contains(target)) {
    std::cout << target << " is a shell builtin" << std::endl;
    return;
  }
  std::string file = is_in_path(target, path);
  if (!file.empty()) {
    std::cout << target << " is " << file << std::endl;
  } else {
    std::cerr << target << ": not found" << std::endl;
  }
}

void builtin_pwd() {
  std::error_code ec;
  std::filesystem::path pwd = std::filesystem::current_path(ec);
  if (ec) {
    std::cerr << "cannot print working directory" << std::endl;
    return;
  }
  std::cout << pwd.string() << std::endl;
}

void builtin_cd(userInput& input) {
  if (effective_arg_count(input) != 2) {
    std::cerr << "cd takes exactly one argument" << std::endl;
    return;
  }
  int status;
  if (input.parsed_input[1] == "~") {
    char* user_home = getenv("HOME");
    if (user_home == nullptr) {
      std::cerr << "cd: HOME not set" << std::endl;
      return;
    }
    status = chdir(user_home);
  } else {
    status = chdir(input.parsed_input[1].c_str());
  }
  if (status == 0)
    return;
  std::cerr << "cd: " << input.parsed_input[1] << ": " << strerror(errno) << std::endl;
}

// make a vector of strings into a c based argv[]
std::vector<char*> make_args(const std::vector<std::string>& input) {
  std::vector<char*> result;
  for (auto& p : input) {
    result.push_back(const_cast<char*>(p.c_str()));
  }
  result.push_back(nullptr);
  return result;
}

// Opens (create/truncate) the redirect target for writing. Returns -1 on
// failure, having already reported the error.
int open_redirect_target(const std::string& file_name) {
  int fd = open(file_name.c_str(), O_CREAT | O_TRUNC | O_WRONLY, 0644);
  if (fd == -1) {
    perror("open");
  }
  return fd;
}

// Points target_fd at file_name for the guard's lifetime, restoring target_fd
// to whatever it pointed at before once the guard goes out of scope. Meant
// for builtins, which run in this process and must hand the terminal back
// afterward -- external commands redirect themselves once in the forked
// child instead (see shell_execute), since that process never returns.
class RedirectGuard {
 public:
  RedirectGuard(int target_fd, const std::string& file_name) : target_fd_(target_fd) {
    int new_fd = open_redirect_target(file_name);
    if (new_fd == -1) {
      return;
    }
    saved_fd_ = dup(target_fd_);
    if (saved_fd_ == -1) {
      perror("dup");
      close(new_fd);
      return;
    }
    dup2(new_fd, target_fd_);
    close(new_fd);
    ok_ = true;
  }

  ~RedirectGuard() {
    if (ok_) {
      dup2(saved_fd_, target_fd_);
      close(saved_fd_);
    }
  }

  RedirectGuard(const RedirectGuard&) = delete;
  RedirectGuard& operator=(const RedirectGuard&) = delete;

  bool ok() const { return ok_; }

 private:
  int target_fd_;
  int saved_fd_ = -1;
  bool ok_ = false;
};

// Precondition: validate_redirect_syntax(input) returned true.
void shell_execute(userInput& input, const std::vector<std::string>& path) {
  size_t arg_count = effective_arg_count(input);
  std::vector<std::string> command_args(input.parsed_input.begin(),
                                         input.parsed_input.begin() + arg_count);
  std::string abs_path = is_in_path(command_args[0], path);
  if (abs_path.empty()) {
    std::cerr << command_args[0] << ": command not found" << std::endl;
    return;
  }
  std::vector<char*> arg_vector = make_args(command_args);
  pid_t pid = fork();
  if (pid == 0) {  // in child
    if (input.redirect_position >= 0) {
      const std::string& file_name = input.parsed_input[input.redirect_position + 1];
      int new_fd = open_redirect_target(file_name);
      if (new_fd == -1) {
        _exit(1);
      }
      dup2(new_fd, input.redirect_fd);
      close(new_fd);
    }
    execv(abs_path.c_str(), arg_vector.data());
    _exit(127);  // staying true to bash code conventions
  } else if (pid == -1) {
    std::cerr << "fork failed" << std::endl;
  } else {  // in parent
    waitpid(pid, nullptr, 0);
  }
}

int main() {
  // Flush after every std::cout / std:cerr
  std::cout << std::unitbuf;
  std::cerr << std::unitbuf;
  char* path_raw = getenv("PATH");
  std::string path_string = path_raw ? path_raw : "";
  std::vector<std::string> path_parsed = parse_path(path_string, PATH_LIST_SEPARATOR);
  while (true) {
    std::cout << "$ ";
    userInput user_input;
    std::getline(std::cin, user_input.raw_input);
    user_input.redirect_position = -1;
    user_input.redirect_fd = 1;
    user_input.parsed_input = parse_string(user_input, " ");
    if (user_input.parsed_input.empty()) {
      continue;
    }
    if (!validate_redirect_syntax(user_input)) {
      continue;
    }
    std::string entered_command = user_input.parsed_input[0];
    if (entered_command == "exit") {
      break;
    }

    bool command_is_builtin = builtins.contains(entered_command);

    std::optional<RedirectGuard> redirect_guard;
    if (user_input.redirect_position >= 0 && command_is_builtin) {
      const std::string& file_name = user_input.parsed_input[user_input.redirect_position + 1];
      redirect_guard.emplace(user_input.redirect_fd, file_name);
      if (!redirect_guard->ok()) {
        continue;
      }
    }

    if (entered_command == "type") {
      builtin_type(user_input, path_parsed);
    } else if (entered_command == "echo") {
      builtin_echo(user_input);
    } else if (entered_command == "pwd") {
      builtin_pwd();
    } else if (entered_command == "cd") {
      builtin_cd(user_input);
    } else {
      shell_execute(user_input, path_parsed);
    }
  }
}

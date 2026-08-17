#include <sys/wait.h>
#include <unistd.h>

#include <algorithm>
#include <cctype>
#include <cerrno>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <string>
#include <unordered_set>
#include <vector>

#ifdef _WIN32
constexpr char PATH_LIST_SEPARATOR[] = ";";
#else
constexpr char PATH_LIST_SEPARATOR[] = ":";
#endif

std::unordered_set<std::string> builtins = {"echo", "exit", "type", "pwd", "cd"};

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
std::vector<std::string> parse_string(const std::string& s, const std::string& delim, int& redirect,
                                       int& redirect_fd) {
  std::vector<std::string> result;
  std::string token;
  bool in_single_quotes = false;
  bool in_double_quotes = false;
  bool token_started = false;
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
      redirect_fd = 1;
      if (token_started) {
        bool all_digits = !token.empty() && std::all_of(token.begin(), token.end(), [](unsigned char ch) {
                             return std::isdigit(ch);
                           });
        if (all_digits) {
          redirect_fd = std::stoi(token);
        } else {
          result.push_back(token);
        }
        token.clear();
        token_started = false;
      }
      redirect = static_cast<int>(result.size());
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

void builtin_echo(const std::vector<std::string>& s) {
  for (size_t i = 1; i < s.size(); i++) {
    if (i > 1)
      std::cout << " ";
    std::cout << s[i];
  }
  std::cout << std::endl;
  return;
}

void builtin_type(const std::vector<std::string>& s, const std::vector<std::string>& path) {
  if (builtins.contains(s[1])) {
    std::cout << s[1] << " is a shell builtin" << std::endl;
    return;
  }
  std::string file = is_in_path(s[1], path);
  if (!file.empty()) {
    std::cout << s[1] << " is " << file << std::endl;
    return;
  } else {
    std::cout << s[1] << ": not found" << std::endl;
  }
  return;
}

void builtin_pwd(void) {
  int factor = 1024;
  char* buf = static_cast<char*>(malloc(factor));
  char* pwd = getcwd(buf, factor);
  while (pwd == nullptr) {
    if (errno == ERANGE) {  // buffer too small
      factor *= 2;
      free(buf);
      buf = static_cast<char*>(malloc(factor));
      pwd = getcwd(buf, factor);
    } else {  // something else happened
      free(buf);
      std::cerr << "cannot print working directory" << std::endl;
      return;
    }
  }
  std::cout << pwd << std::endl;
  free(pwd);
  return;
}

void builtin_cd(const std::vector<std::string>& input) {
  if (input.size() != 2) {
    std::cout << "cd takes exactly one argument" << std::endl;
    return;
  }
  char* user_home = getenv("HOME");
  int status;
  if (input[1] == "~") {
    status = chdir(user_home);
  } else {
    status = chdir(input[1].c_str());
  }
  if (status == 0)
    return;
  std::cout << "cd: " << input[1] << ": " << strerror(errno) << std::endl;
  return;
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

// not a void probably
void shell_execute(const std::string& command, const std::vector<std::string>& args) {
  std::vector<char*> arg_vector = make_args(args);
  int status;
  pid_t pid = fork();
  switch (pid) {
    case 0:  // in child
      execv(command.c_str(), arg_vector.data());
      _exit(127);  // staying true to bash code conventions
      break;
    case -1:  // fork failed
      std::cout << "fork failed" << std::endl;
      break;
    default:  // in parent
      pid = wait(&status);
  }
  return;
}

void shell_redirect(std::vector<std::string>& input, int redirect, int redirect_fd,
                     const std::vector<std::string>& path) {
  (void)redirect_fd;  // not wired into an open()/dup2() step yet
  std::vector<std::string> command_part(input.begin(), input.begin() + redirect);
  std::string file = is_in_path(command_part[0], path);
  if (!file.empty()) {
    shell_execute(file, command_part);
  } else {
    std::cout << command_part[0] << ": command not found" << std::endl;
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
    std::string command;
    std::getline(std::cin, command);
    int redirect = -1;
    int redirect_fd = 1;
    auto input = parse_string(command, " ", redirect, redirect_fd);
    if (input.empty()) {
      continue;
    }
    if (redirect >= 0) {
      shell_redirect(input, redirect, redirect_fd, path_parsed);
      continue;
    }
    if (input[0] == "type") {
      builtin_type(input, path_parsed);
      continue;
    }
    if (input[0] == "exit")
      break;
    if (input[0] == "echo") {
      builtin_echo(input);
      continue;
    }
    if (input[0] == "pwd") {
      builtin_pwd();
      continue;
    }

    if (input[0] == "cd") {
      builtin_cd(input);
      continue;
    }
    std::string file = is_in_path(input[0], path_parsed);
    if (!file.empty()) {  // there is an executable in path
      shell_execute(file, input);
    } else {
      std::cout << command << ": command not found" << std::endl;
    }
  }
}

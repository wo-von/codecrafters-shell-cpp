#include <sys/wait.h>
#include <unistd.h>

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

std::vector<std::string> parse_string(const std::string& s, const std::string& delim) {
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
  int status = chdir(input[1].c_str());
  if (status == 0)
    return;
  std::cout << "cd: " << input[1] << ": " << strerror(errno) << std::endl;
  return;
}

std::vector<char*> make_args(const std::vector<std::string>& input) {
  std::vector<char*> result;
  for (auto& p : input) {
    result.push_back(const_cast<char*>(p.c_str()));
  }
  result.push_back(nullptr);
  return result;
}
// not a void probably
void shell_execute(const std::string& file, const std::vector<std::string>& input) {
  std::vector<char*> arg_vector = make_args(input);
  int status;
  pid_t pid = fork();
  switch (pid) {
    case 0:  // in child
      execv(file.c_str(), arg_vector.data());
      _exit(127);
      break;
    case -1:  // fork failed
      std::cout << "fork failed" << std::endl;
      break;
    default:  // in parent
      pid = wait(&status);
  }
  return;
}

int main() {
  // Flush after every std::cout / std:cerr
  std::cout << std::unitbuf;
  std::cerr << std::unitbuf;
  char* path_raw = getenv("PATH");
  std::string path_string = path_raw ? path_raw : "";
  std::vector<std::string> path_parsed = parse_string(path_string, PATH_LIST_SEPARATOR);
  while (true) {
    std::cout << "$ ";
    std::string command;
    std::getline(std::cin, command);
    auto input = parse_string(command, " ");
    if (input.empty()) {
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

#include <iostream>
#include <string>
#include <vector>
#include <unordered_set>

std::unordered_set<std::string>builtsins = {"echo", "exit", "type"};
std::vector<std::string> parse_input(const std::string& s, const std::string& delim) {
  std::vector<std::string> result;
  size_t start = 0, end;
  while((end = s.find(delim, start)) != std::string::npos) {
    std::string token = s.substr(start, end - start);
    if(!token.empty()){
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
    std::cout << s[i] << " ";
  }
  std::cout << std::endl;
  return;
}
 void builtin_type(const std::vector<std::string>& s) {
  if (builtsins.contains(s[1])){
    std::cout << s[1] << " is a shell builtin" << std::endl;
  } else {
    std::cout << s[1] << ": not found" << std::endl;
  }
  return;
 }

int main() {
  // Flush after every std::cout / std:cerr
  std::cout << std::unitbuf;
  std::cerr << std::unitbuf;
  while(true) {
    std::cout << "$ ";
    std::string command;
    std::getline(std::cin, command);
    auto input = parse_input(command, " ");
    if (input.empty()){
      continue;
    }
    if (input[0] == "type"){
      builtin_type(input);
      continue;
    }
    if (input[0] == "exit")
      break;
    if (input[0] == "echo"){
      builtin_echo(input);
      continue;
    }
    std::cout << command << ": command not found" << std::endl;
  }
}

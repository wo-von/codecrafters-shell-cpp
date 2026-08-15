#include <unistd.h>

#include <cstdlib>
#include <iostream>
#include <string>
#include <unordered_set>
#include <vector>

using namespace std;

#ifdef _WIN32
constexpr char PATH_LIST_SEPARATOR[] = ";";
#else
constexpr char PATH_LIST_SEPARATOR[] = ":";
#endif

unordered_set<string> builtsins = {"echo", "exit", "type"};

vector<string> parse_string(const string& s, const string& delim) {
  vector<string> result;
  size_t start = 0, end;
  while ((end = s.find(delim, start)) != string::npos) {
    string token = s.substr(start, end - start);
    if (!token.empty()) {
      result.push_back(token);
    }
    start = end + delim.size();
  }
  string last = s.substr(start);
  if (!last.empty()) {
    result.push_back(last);
  }
  return result;
}

void builtin_echo(const vector<string>& s) {
  for (size_t i = 1; i < s.size(); i++) {
    cout << s[i] << " ";
  }
  cout << endl;
  return;
}
void builtin_type(const vector<string>& s, const vector<string>& path) {
  if (builtsins.contains(s[1])) {
    cout << s[1] << " is a shell builtin" << endl;
    return;
  }
  for (auto p : path) {
    const char* file = p.append("/").append(s[1]).c_str();
    if (!access(file, X_OK)) {
      cout << s[1] << " is " << file << endl;
      return;
    }
  }
  cout << s[1] << ": command not found" << endl;
  return;
}

int main() {
  // Flush after every std::cout / std:cerr
  cout << unitbuf;
  cerr << unitbuf;
  char* path_raw = getenv("PATH");
  string path_string = path_raw ? path_raw : "";
  vector<string> path_parsed = parse_string(path_string, PATH_LIST_SEPARATOR);
  while (true) {
    cout << "$ ";
    string command;
    getline(cin, command);
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
    cout << command << ": command not found" << endl;
  }
}

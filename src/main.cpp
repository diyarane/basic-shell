#include <iostream>
#include <string>
#include <sstream>
#include <vector>
using namespace std;

int main() {
  // Flush after every std::cout / std::cerr
  cout << unitbuf;
  cerr << unitbuf;

  while (true) {
    cout << "$ ";
    string input;
    getline(cin, input);

    // Split input into words
    vector<string> parts;
    string word;
    stringstream ss(input);
    while (ss >> word) parts.push_back(word);

    if (parts.empty()) continue;

    if (input == "exit 0") return 0;
    else if (parts[0] == "echo") {
      for (size_t i = 1; i < parts.size(); ++i)
        cout << parts[i] << (i + 1 < parts.size() ? " " : "\n");
    }
    else if (parts[0] == "type") {
      if (parts.size() < 2)
        cout << "type: missing argument\n";
      else if (parts[1] == "echo" || parts[1] == "exit" || parts[1] == "type")
        cout << parts[1] << " is a shell builtin\n";
      else
        cout << parts[1] << ": not found\n";
    }
    else
      cout << input << ": command not found\n";
  }

  return 0;
}

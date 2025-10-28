#include <iostream>
#include <string>


int main() {
  // Flush after every std::cout / std:cerr
  std::cout << std::unitbuf;
  std::cerr << std::unitbuf;
  while(true){
    // TODO: Uncomment the code below to pass the first stage
    std::cout << "$ ";

    std::string input;
    std::getline(std::cin, input);
    if(input == "exit 0") return 0;
    else if (input.find("echo") != std::string::npos) std::cout << input.substr(5) << "\n";

    else std::cout << input << ": command not found" << std::endl;
  }
  return 0;
}

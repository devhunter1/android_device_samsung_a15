#include <algorithm>
#include <cstdlib>
#include <cstring>
#include <fcntl.h>
#include <iostream>
#include <limits.h>
#include <linux/fs.h>
#include <string>
#include <sys/ioctl.h>
#include <sys/stat.h>
#include <sys/sysinfo.h>
#include <sys/types.h>
#include <unistd.h>
#include <vector>

#define CODE_FAILURE -1
#define USER_FAILURE -2
#define MAX_MEMORY_PERCENT 60

unsigned long long getTotalSystemMemory() {
  struct sysinfo info;
  if (sysinfo(&info) != 0) {
    printf("Error getting system info\n");
    exit(CODE_FAILURE);
  }
  return info.totalram * info.mem_unit;
}

std::vector<uint8_t> str2hex(const std::string &text) {
  std::vector<uint8_t> hexVec;
  for (char c : text) {
    uint8_t hex = static_cast<uint8_t>(c);
    hexVec.push_back(hex);
  }
  return hexVec;
}

std::string realPath(std::string filePath, bool beQuiet) {
  char resolvedPath[PATH_MAX];
  if (realpath(filePath.data(), resolvedPath) == nullptr) {
    if (!beQuiet) {
      std::cerr << "Failed to resolve realpath for " << filePath << std::endl;
    }
    exit(CODE_FAILURE);
  }
  return resolvedPath;
}

int replacebinary(std::string filePath, std::vector<std::string> replacements,
                  bool beQuiet) {
  ssize_t replacedCount = 0;
  filePath = realPath(filePath, beQuiet);

  struct stat sb;
  int fd = open(filePath.data(), O_RDWR);
  if (fd == -1) {
    if (!beQuiet) {
      std::cerr << "Failed to open " << filePath << std::endl;
    }
    return CODE_FAILURE;
  }
  if (lstat(filePath.data(), &sb) == -1) {
    if (!beQuiet) {
      std::cerr << "Failed to lstat " << filePath << std::endl;
    }
    close(fd);
    return CODE_FAILURE;
  }

  bool isBlock = S_ISBLK(sb.st_mode);
  bool isRegular = S_ISREG(sb.st_mode);
  if (!isBlock && !isRegular) {
    if (!beQuiet) {
      std::cerr << "Error: '" << filePath
                << "' is neither a regular file nor a block device."
                << std::endl;
    }
    return USER_FAILURE;
  }
  unsigned long long fdSize;
  if (isBlock) {
    if (ioctl(fd, BLKGETSIZE64, &fdSize) < 0) {
      if (!beQuiet) {
        std::cerr << "Failed to get size of " << filePath << std::endl;
      }
      close(fd);
      return CODE_FAILURE;
    }
  } else {
    fdSize = static_cast<unsigned long long>(sb.st_size);
  }

  auto it = std::max_element(replacements.begin(), replacements.end(),
                             [](const std::string &a, const std::string &b) {
                               return a.length() < b.length();
                             });

  if (fdSize < it->length()) {
    if (!beQuiet) {
      std::cerr << "Invalid file size: " << std::to_string(fdSize) << std::endl;
    }
    close(fd);
    return CODE_FAILURE;
  }

  if (fdSize > getTotalSystemMemory() / 100 * MAX_MEMORY_PERCENT) {
    if (!beQuiet) {
      std::cout << "Error: This file is too big to fit in your memory."
                << std::endl;
    }
    return USER_FAILURE;
  }

  uint8_t *buffer = new uint8_t[fdSize];
  if (buffer == nullptr) {
    if (!beQuiet) {
      std::cerr << "Error: Failed to allocate memory for file contents."
                << std::endl;
    }
    return CODE_FAILURE;
  }

  ssize_t bytesRead = 0;
  ssize_t totalBytesRead = 0;
  while ((bytesRead = read(
              fd, buffer + totalBytesRead,
              fdSize - static_cast<unsigned long long>(totalBytesRead))) > 0) {
    totalBytesRead += bytesRead;
  }
  if (bytesRead == -1) {
    if (!beQuiet) {
      std::cerr << "Failed to read " << filePath << std::endl;
    }
    replacedCount = CODE_FAILURE;
    goto done;
  }

  for (unsigned long i = 0; i < replacements.size(); i += 2) {
    const std::vector<uint8_t> &from = str2hex(replacements[i]);
    const std::vector<uint8_t> &to = str2hex(replacements[i + 1]);
    if (from.size() != to.size()) {
      std::cout << "Invalid replacement." << std::endl;
      continue;
    }
    ssize_t pos = 0;
    while ((pos = std::search(buffer + pos, buffer + fdSize, from.begin(),
                              from.end()) -
                  buffer) != static_cast<long long>(fdSize)) {
      if (lseek(fd, pos, SEEK_SET) == -1) {
        if (!beQuiet) {
          perror("lseek() failed");
        }
        replacedCount = CODE_FAILURE;
        goto done;
      }
      ssize_t bytesWritten = write(fd, to.data(), to.size());
      if (bytesWritten == -1 ||
          bytesWritten != static_cast<ssize_t>(to.size())) {
        if (!beQuiet) {
          perror("write() failed");
        }
        replacedCount = CODE_FAILURE;
        goto done;
      }
      pos += from.size();
      ++replacedCount;
    }
  }

  if (!beQuiet) {
    std::cout << "Replaced " << replacedCount << " occurrences." << std::endl;
  }

done:
  delete[] buffer;
  close(fd);
  return static_cast<int>(replacedCount);
}

void usage(char *executable) {
  std::cout << "    Usage: " << executable
            << " [--quiet|-q] <filePath> <from|to> [from|to] ..." << std::endl;
  std::cout << "    'from' and 'to' should have same the length in order to be "
               "replaced."
            << std::endl
            << std::endl;
  std::cout << "    Example: " << executable << " myfile.bin 'system|vendor'"
            << std::endl;
  std::cout
      << "      All occurrences of 'system' will be replaced with 'vendor'"
      << std::endl;
}

int main(int argc, char **argv) {
  bool beQuiet = false;
  for (int i = 1; i < argc; ++i) {
    if (std::strcmp(argv[i], "--quiet") == 0 ||
        std::strcmp(argv[i], "-q") == 0) {
      beQuiet = true;
      for (int j = i; j < argc - 1; ++j) {
        std::memmove(argv + j, argv + j + 1, sizeof(char *));
      }
      --argc;
      --i;
    }
  }

  if (argc < 3) {
    usage(argv[0]);
    return USER_FAILURE;
  }
  std::vector<std::string> args(argv + 2, argv + argc);
  std::vector<std::string> replacements;
  for (const auto &arg : args) {
    ssize_t delimiter_count = std::count(arg.begin(), arg.end(), '|');
    if (delimiter_count != 1) {
      if (!beQuiet) {
        std::cout << "Ignored argument: '" << arg << "'" << std::endl;
      }
      continue;
    }

    size_t delimiter_pos = arg.find('|');
    std::string first_part = arg.substr(0, delimiter_pos);
    std::string second_part = arg.substr(delimiter_pos + 1);
    if (first_part.length() == second_part.length() &&
        first_part != second_part) {
      replacements.push_back(first_part);
      replacements.push_back(second_part);
    } else {
      if (!beQuiet) {
        std::cout << "Invalid argument: '" << arg << "'" << std::endl;
      }
    }
  }
  if (replacements.size() == 0) {
    if (!beQuiet) {
      std::cout << "Nothing to replace." << std::endl;
    }
    return USER_FAILURE;
  }
  return replacebinary(argv[1], replacements, beQuiet);
}

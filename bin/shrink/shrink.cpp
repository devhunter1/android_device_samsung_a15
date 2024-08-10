#include <cstring>
#include <fcntl.h>
#include <iostream>
#include <libgen.h>
#include <sys/stat.h>
#include <unistd.h>

const size_t CHUNK_SIZE = 2 * 1024 * 1024;

int shrink(const char *file, const char *exe, bool beQuiet) {
  char *base = basename(const_cast<char *>(file));
  long long file_size, last_non_zero_byte, shrunk_bytes;
  int shrunk = 0;

  if (!beQuiet) {
    std::cout << " - " << exe << ": Shrinking '" << base << "'" << std::endl;
  }
  int fd = open(file, O_RDWR);
  if (fd == -1) {
    if (!beQuiet) {
      std::cerr << " ! " << exe << ": Failed to open file." << std::endl;
    }
    return shrunk;
  }

  struct stat st;
  if (fstat(fd, &st) == -1) {
    if (!beQuiet) {
      std::cerr << " ! " << exe << ": Failed to stat file." << std::endl;
    }
    goto finish;
  }

  if (!S_ISREG(st.st_mode)) {
    if (!beQuiet) {
      std::cerr << " ! " << exe << ": Not a regular file." << std::endl;
    }
    goto finish;
  }

  file_size = st.st_size;
  if (file_size < 1) {
    if (!beQuiet) {
      std::cerr << " ! " << exe << ": Nothing to shrink in an empty file."
                << std::endl;
    }
    goto finish;
  }

  if (lseek(fd, -1L, SEEK_END) == -1) {
    if (!beQuiet) {
      std::cerr << " ! " << exe << ": Failed to seek to end of file."
                << std::endl;
    }
    goto finish;
  }

  char last_byte;
  if (read(fd, &last_byte, sizeof(char)) != 1) {
    if (!beQuiet) {
      std::cerr << " ! " << exe << ": Failed to read last byte of file."
                << std::endl;
    }
    goto finish;
  }

  if (last_byte != '\0') {
    if (!beQuiet) {
      std::cout << " - " << exe << ": File is already shrunk." << std::endl;
    }
    goto finish;
  }

  last_non_zero_byte = file_size - 1;
  char buffer[CHUNK_SIZE];

  for (off_t pos = file_size - static_cast<long long>(CHUNK_SIZE); pos >= 0;
       pos -= CHUNK_SIZE) {
    ssize_t n = pread(fd, buffer, CHUNK_SIZE, pos);
    if (n == -1) {
      if (!beQuiet) {
        std::cerr << " ! " << exe << ": Failed to read file" << std::endl;
      }
      goto finish;
    }

    for (off_t i = n - 1; i >= 0; --i) {
      if (buffer[i] != '\0') {
        last_non_zero_byte = pos + i;
        goto done;
      }
    }
  }

done:
  if (ftruncate(fd, last_non_zero_byte + 1) == -1) {
    if (!beQuiet) {
      std::cerr << " ! " << exe << ": Failed to shrink " << base << std::endl;
    }
    goto finish;
  }

  shrunk_bytes = static_cast<long long>(last_non_zero_byte + 1);

  if (file_size - shrunk_bytes < 1) {
    if (!beQuiet) {
      std::cout << " - " << exe << ": Could not shrink." << std::endl;
    }
    goto finish;
  }

  if (!beQuiet) {
    std::cout << " * " << exe << ": Cut off " << file_size - shrunk_bytes
              << " Bytes";
    if (shrunk_bytes > 2 * 1024 * 1024) {
      std::cout << " (" << (file_size - shrunk_bytes) / 1024 / 1024
                << " MegaBytes)";
    }
    std::cout << std::endl;
    std::cout << " * " << exe << ": Shrunk to " << shrunk_bytes << " Bytes";
    if (shrunk_bytes > 2 * 1024 * 1024) {
      std::cout << " (" << shrunk_bytes / 1024 / 1024 << " MegaBytes)";
    }
    std::cout << std::endl;
  }
  shrunk = 1;

finish:
  close(fd);
  return shrunk;
}

int main(int argc, char *argv[]) {
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

  if (argc < 2) {
    std::cerr << " - Usage: " << argv[0]
              << " [--quiet|-q] <filename> [filename] ..." << std::endl;
    return 0;
  }

  int ret = 0;
  for (int i = 1; i < argc; i++) {
    ret += shrink(argv[i], argv[0], beQuiet);
  }
  return ret;
}

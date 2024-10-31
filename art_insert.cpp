#include <iostream>
#include <string>
#include <vector>
#include <sys/stat.h>
#include <fcntl.h>
#include <malloc/malloc.h>

#include "cpp_src/art.hpp"

using namespace std;

double time_taken_in_secs(clock_t t) {
  t = clock() - t;
  return ((double)t)/CLOCKS_PER_SEC;
}

clock_t print_time_taken(clock_t t, const char *msg) {
  double time_taken = time_taken_in_secs(t); // in seconds
  std::cout << msg << time_taken << std::endl;
  return clock();
}

int main(int argc, char *argv[]) {

  art::art_trie at;
  std::vector<uint8_t *> lines;

  clock_t t = clock();

  struct stat file_stat;
  memset(&file_stat, '\0', sizeof(file_stat));
  stat(argv[1], &file_stat);
  uint8_t *file_buf = (uint8_t *) malloc(file_stat.st_size + 1);
  printf("File_size: %lld\n", file_stat.st_size);

  FILE *fp = fopen(argv[1], "rb");
  if (fp == NULL) {
    perror("Could not open file; ");
    free(file_buf);
    return 1;
  }
  size_t res = fread(file_buf, 1, file_stat.st_size, fp);
  if (res != file_stat.st_size) {
    perror("Error reading file: ");
    free(file_buf);
    return 1;
  }
  fclose(fp);

  int line_count = 0;
  uint8_t *prev_line = {0};
  size_t prev_line_len = 0;
  uint8_t *line = file_buf;
  uint8_t *cr_pos = (uint8_t *) memchr(line, '\n', file_stat.st_size);
  if (cr_pos == NULL)
    cr_pos = file_buf + file_stat.st_size;
  else {
    if (cr_pos > line && *(cr_pos - 1) == '\r')
      cr_pos--;
  }
  *cr_pos = 0;
  size_t line_len = cr_pos - line;
  do {
    if (prev_line_len != line_len || strncmp((const char *) line, (const char *) prev_line, prev_line_len) != 0) {
      at.art_insert(line, line_len, line);
      lines.push_back(line);
      prev_line = line;
      prev_line_len = line_len;
      line_count++;
      if ((line_count % 100000) == 0) {
        cout << ".";
        cout.flush();
      }
    }
    line = cr_pos + (cr_pos[1] == '\n' ? 2 : 1);
    cr_pos = (uint8_t *) memchr(line, '\n', file_stat.st_size - (cr_pos - file_buf));
    if (cr_pos != NULL && cr_pos > line) {
      if (*(cr_pos - 1) == '\r')
        cr_pos--;
      *cr_pos = 0;
      line_len = cr_pos - line;
    }
  } while (cr_pos != NULL);
  std::cout << std::endl;
  t = print_time_taken(t, "Time taken for insert/append: ");
  printf("ART Size: %lu\n", at.art_size_in_bytes());

  uint8_t val_buf[100];

  int err_count = 0;
  line_count = 0;
  bool success = false;
  for (int i = 0; i < lines.size(); i++) {
    line = lines[i];
    line_len = strlen((const char *) line);
    uint8_t *res_val = (uint8_t *) at.art_search(line, line_len);
    if (res_val != NULL) {
      int val_len = line_len > 6 ? 7 : line_len;
      memcpy(val_buf, res_val, val_len);
      if (memcmp(line, val_buf, val_len) != 0) {
        printf("key: [%.*s], val: [%.*s]\n", (int) line_len, line, val_len, res_val);
        err_count++;
      }
    } else {
      std::cout << "Not found: " << line << std::endl;
      err_count++;
    }
    line_count++;
  }
  printf("\nKeys per sec: %lf\n", line_count / time_taken_in_secs(t) / 1000);
  t = print_time_taken(t, "Time taken for retrieve: ");
  printf("Lines: %d, Errors: %d\n", line_count, err_count);

  free(file_buf);

}

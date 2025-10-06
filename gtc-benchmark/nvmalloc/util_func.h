
int rand_word(char *input, int len);
unsigned int gen_id_from_str(char *key);
void sha1_mykeygen(void *key, char *digest, size_t size,
                    int base, size_t input_size);

int check_modify_access(int perm);
long simulation_time(struct timeval start, struct timeval end );
int memcpy_delay(void *dest, void *src, size_t len);

#ifndef TOML_H
#define TOML_H

#include <stdio.h>

typedef struct TomlKeyVal TomlKeyVal;
typedef struct TomlArray TomlArray;
typedef struct TomlArrayKey TomlArrayKey;

struct TomlKeyVal {
	char *key;
	char *val;
};

struct TomlArray {
	TomlArray *parentarr;

	TomlKeyVal **item;
	TomlArrayKey **key;

	size_t nitem;
	size_t nkey;
};

struct TomlArrayKey {
	char *key;

	TomlArray **arr;
	size_t narr;
};

void tomldeletearray(TomlArray *arr);
TomlArrayKey* tomlgetarraykey(TomlArray *arr, const char *key);
int tomlgetbool(TomlArray *arr, const char *key, unsigned int *ret);
TomlArray* tomlgetconfig(FILE *fp);
int tomlgetdouble(TomlArray *arr, const char *key, double *ret);
int tomlgetstring(TomlArray *arr, const char *key, char **ret);
int tomlgetuint(TomlArray *arr, const char *key, unsigned int *ret);


#endif /* TOML_H */

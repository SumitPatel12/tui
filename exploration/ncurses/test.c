#include <locale.h>
#include <stdio.h>
#include <langinfo.h>

int main() {
	printf("Locale is: %s\n", setlocale(LC_ALL, ""));
	printf("nl_langinfo: %s\n", nl_langinfo(CODESET));
	return 0;
}

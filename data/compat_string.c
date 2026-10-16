#include <stddef.h>
#include <stdint.h>

/* --- strlen & strnlen --- */
size_t strlen(const char *s)
{
    size_t len = 0;
    while (s[len]) len++;
    return len;
}
size_t __strlen_asimd(const char *s) __attribute__((alias("strlen")));
size_t __strlen_mte(const char *s) __attribute__((alias("strlen")));
size_t __strlen(const char *s) __attribute__((alias("strlen")));

size_t strnlen(const char *s, size_t maxlen)
{
    size_t len = 0;
    while (len < maxlen && s[len]) len++;
    return len;
}

/* --- memcpy & mempcpy --- */
void *memcpy(void *dest, const void *src, size_t n)
{
    uint8_t *d = (uint8_t *)dest;
    const uint8_t *s = (const uint8_t *)src;
    for (size_t i = 0; i < n; i++) d[i] = s[i];
    return dest;
}
void *__memcpy_generic(void *dest, const void *src, size_t n) __attribute__((alias("memcpy")));
void *__memcpy_simd(void *dest, const void *src, size_t n) __attribute__((alias("memcpy")));
void *__memcpy_thunderx(void *dest, const void *src, size_t n) __attribute__((alias("memcpy")));
void *__memcpy_thunderx2(void *dest, const void *src, size_t n) __attribute__((alias("memcpy")));
void *__memcpy_falkor(void *dest, const void *src, size_t n) __attribute__((alias("memcpy")));
void *__memcpy_a64fx(void *dest, const void *src, size_t n) __attribute__((alias("memcpy")));
void *__memcpy_chk(void *dest, const void *src, size_t n, size_t destlen) { (void)destlen; return memcpy(dest, src, n); }

void *mempcpy(void *dest, const void *src, size_t n)
{
    memcpy(dest, src, n);
    return (uint8_t *)dest + n;
}
void *__mempcpy(void *dest, const void *src, size_t n) __attribute__((alias("mempcpy")));
void *__mempcpy_chk(void *dest, const void *src, size_t n, size_t destlen) { (void)destlen; return mempcpy(dest, src, n); }

/* --- memmove --- */
void *memmove(void *dest, const void *src, size_t n)
{
    uint8_t *d = (uint8_t *)dest;
    const uint8_t *s = (const uint8_t *)src;
    if (d < s) {
        for (size_t i = 0; i < n; i++) d[i] = s[i];
    } else if (d > s) {
        for (size_t i = n; i > 0; i--) d[i - 1] = s[i - 1];
    }
    return dest;
}
void *__memmove_chk(void *dest, const void *src, size_t n, size_t destlen) { (void)destlen; return memmove(dest, src, n); }

/* --- memset & bzero --- */
void *memset(void *s, int c, size_t n)
{
    uint8_t *p = (uint8_t *)s;
    for (size_t i = 0; i < n; i++) p[i] = (uint8_t)c;
    return s;
}
void *__memset_generic(void *s, int c, size_t n) __attribute__((alias("memset")));
void *__memset_falkor(void *s, int c, size_t n) __attribute__((alias("memset")));
void *__memset_emag(void *s, int c, size_t n) __attribute__((alias("memset")));
void *__memset_kunpeng(void *s, int c, size_t n) __attribute__((alias("memset")));
void *__memset_a64fx(void *s, int c, size_t n) __attribute__((alias("memset")));
void *__memset_chk(void *s, int c, size_t n, size_t destlen) { (void)destlen; return memset(s, c, n); }

void bzero(void *s, size_t n)
{
    memset(s, 0, n);
}
void __bzero(void *s, size_t n) __attribute__((alias("bzero")));

void bcopy(const void *src, void *dest, size_t n)
{
    memmove(dest, src, n);
}

/* --- memcmp & bcmp --- */
int memcmp(const void *s1, const void *s2, size_t n)
{
    const uint8_t *p1 = (const uint8_t *)s1;
    const uint8_t *p2 = (const uint8_t *)s2;
    for (size_t i = 0; i < n; i++) {
        if (p1[i] != p2[i]) return (int)p1[i] - (int)p2[i];
    }
    return 0;
}
int bcmp(const void *s1, const void *s2, size_t n) __attribute__((alias("memcmp")));

int __memcmpeq(const void *s1, const void *s2, size_t n)
{
    return memcmp(s1, s2, n);
}
int memcmpeq(const void *s1, const void *s2, size_t n) __attribute__((alias("__memcmpeq")));

/* --- memchr & rawmemchr --- */
void *memchr(const void *s, int c, size_t n)
{
    const uint8_t *p = (const uint8_t *)s;
    uint8_t uc = (uint8_t)c;
    for (size_t i = 0; i < n; i++) {
        if (p[i] == uc) return (void *)(p + i);
    }
    return NULL;
}
void *__memchr_generic(const void *s, int c, size_t n) __attribute__((alias("memchr")));
void *__memchr_nosimd(const void *s, int c, size_t n) __attribute__((alias("memchr")));
void *__memchr(const void *s, int c, size_t n) __attribute__((alias("memchr")));

void *rawmemchr(const void *s, int c)
{
    const uint8_t *p = (const uint8_t *)s;
    uint8_t uc = (uint8_t)c;
    while (*p != uc) p++;
    return (void *)p;
}
void *__rawmemchr(const void *s, int c) __attribute__((alias("rawmemchr")));

void *memrchr(const void *s, int c, size_t n)
{
    const uint8_t *p = (const uint8_t *)s;
    uint8_t uc = (uint8_t)c;
    while (n--) {
        if (p[n] == uc) return (void *)(p + n);
    }
    return NULL;
}

/* --- strcpy, stpcpy, strncpy, stpncpy --- */
char *strcpy(char *dest, const char *src)
{
    size_t i = 0;
    while ((dest[i] = src[i]) != '\0') i++;
    return dest;
}
char *__strcpy_chk(char *dest, const char *src, size_t destlen) { (void)destlen; return strcpy(dest, src); }

char *stpcpy(char *dest, const char *src)
{
    while ((*dest = *src) != '\0') {
        dest++;
        src++;
    }
    return dest;
}
char *__stpcpy(char *dest, const char *src) __attribute__((alias("stpcpy")));
char *__stpcpy_chk(char *dest, const char *src, size_t destlen) { (void)destlen; return stpcpy(dest, src); }

char *strncpy(char *dest, const char *src, size_t n)
{
    size_t i = 0;
    for (; i < n && src[i] != '\0'; i++) dest[i] = src[i];
    for (; i < n; i++) dest[i] = '\0';
    return dest;
}
char *__strncpy_chk(char *dest, const char *src, size_t n, size_t destlen) { (void)destlen; return strncpy(dest, src, n); }

char *stpncpy(char *dest, const char *src, size_t n)
{
    size_t i = 0;
    for (; i < n && src[i] != '\0'; i++) dest[i] = src[i];
    char *ret = &dest[i];
    for (; i < n; i++) dest[i] = '\0';
    return ret;
}
char *__stpncpy(char *dest, const char *src, size_t n) __attribute__((alias("stpncpy")));
char *__stpncpy_chk(char *dest, const char *src, size_t n, size_t destlen) { (void)destlen; return stpncpy(dest, src, n); }

/* --- strcmp & strncmp --- */
int strcmp(const char *s1, const char *s2)
{
    while (*s1 && (*s1 == *s2)) {
        s1++;
        s2++;
    }
    return *(const unsigned char *)s1 - *(const unsigned char *)s2;
}

int strncmp(const char *s1, const char *s2, size_t n)
{
    while (n && *s1 && (*s1 == *s2)) {
        s1++;
        s2++;
        n--;
    }
    if (n == 0) return 0;
    return *(const unsigned char *)s1 - *(const unsigned char *)s2;
}

/* --- strcasecmp & strncasecmp --- */
static inline int to_lower(int c)
{
    return (c >= 'A' && c <= 'Z') ? (c + ('a' - 'A')) : c;
}

int strcasecmp(const char *s1, const char *s2)
{
    while (*s1 && (to_lower((unsigned char)*s1) == to_lower((unsigned char)*s2))) {
        s1++;
        s2++;
    }
    return to_lower((unsigned char)*s1) - to_lower((unsigned char)*s2);
}
int __strcasecmp(const char *s1, const char *s2) __attribute__((alias("strcasecmp")));

int strncasecmp(const char *s1, const char *s2, size_t n)
{
    while (n && *s1 && (to_lower((unsigned char)*s1) == to_lower((unsigned char)*s2))) {
        s1++;
        s2++;
        n--;
    }
    if (n == 0) return 0;
    return to_lower((unsigned char)*s1) - to_lower((unsigned char)*s2);
}
int __strncasecmp(const char *s1, const char *s2, size_t n) __attribute__((alias("strncasecmp")));

/* --- strchr, strrchr, strchrnul --- */
char *strchr(const char *s, int c)
{
    char ch = (char)c;
    while (*s != ch) {
        if (*s == '\0') return NULL;
        s++;
    }
    return (char *)s;
}

char *strrchr(const char *s, int c)
{
    char ch = (char)c;
    const char *last = NULL;
    do {
        if (*s == ch) last = s;
    } while (*s++);
    return (char *)last;
}

char *strchrnul(const char *s, int c)
{
    char ch = (char)c;
    while (*s && *s != ch) s++;
    return (char *)s;
}
char *__strchrnul(const char *s, int c) __attribute__((alias("strchrnul")));

/* --- strcat & strncat --- */
char *strcat(char *dest, const char *src)
{
    char *rdest = dest;
    while (*dest) dest++;
    while ((*dest++ = *src++));
    return rdest;
}
char *__strcat_chk(char *dest, const char *src, size_t destlen) { (void)destlen; return strcat(dest, src); }

char *strncat(char *dest, const char *src, size_t n)
{
    char *rdest = dest;
    while (*dest) dest++;
    while (n-- && (*dest++ = *src++));
    *dest = '\0';
    return rdest;
}
char *__strncat_chk(char *dest, const char *src, size_t n, size_t destlen) { (void)destlen; return strncat(dest, src, n); }

/* --- strstr & strcasestr --- */
char *strstr(const char *haystack, const char *needle)
{
    if (!*needle) return (char *)haystack;
    for (; *haystack; haystack++) {
        if (*haystack == *needle) {
            const char *h = haystack;
            const char *n = needle;
            while (*h && *n && *h == *n) {
                h++;
                n++;
            }
            if (!*n) return (char *)haystack;
        }
    }
    return NULL;
}

char *strcasestr(const char *haystack, const char *needle)
{
    if (!*needle) return (char *)haystack;
    for (; *haystack; haystack++) {
        if (to_lower((unsigned char)*haystack) == to_lower((unsigned char)*needle)) {
            const char *h = haystack;
            const char *n = needle;
            while (*h && *n && to_lower((unsigned char)*h) == to_lower((unsigned char)*n)) {
                h++;
                n++;
            }
            if (!*n) return (char *)haystack;
        }
    }
    return NULL;
}

/* --- strspn, strcspn, strpbrk, strsep --- */
size_t strspn(const char *s, const char *accept)
{
    size_t count = 0;
    while (*s) {
        const char *a = accept;
        while (*a && *a != *s) a++;
        if (!*a) break;
        count++;
        s++;
    }
    return count;
}

size_t strcspn(const char *s, const char *reject)
{
    size_t count = 0;
    while (*s) {
        const char *r = reject;
        while (*r) {
            if (*r == *s) return count;
            r++;
        }
        count++;
        s++;
    }
    return count;
}

char *strpbrk(const char *s, const char *accept)
{
    while (*s) {
        const char *a = accept;
        while (*a) {
            if (*a == *s) return (char *)s;
            a++;
        }
        s++;
    }
    return NULL;
}

char *strsep(char **stringp, const char *delim)
{
    if (!stringp || !*stringp) return NULL;
    char *begin = *stringp;
    char *end = strpbrk(begin, delim);
    if (end) {
        *end = '\0';
        *stringp = end + 1;
    } else {
        *stringp = NULL;
    }
    return begin;
}

/* Provide memcpy for MSVC linker when using /nodefaultlib */
#pragma function(memcpy)
void *memcpy(void *dst, const void *src, unsigned int size)
{
    unsigned char *d = (unsigned char *)dst;
    const unsigned char *s = (const unsigned char *)src;
    while (size--) *d++ = *s++;
    return dst;
}

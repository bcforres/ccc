//test return -272716322
typedef union foo {
    int x;
    char arr[sizeof(int)];
} foo;

union foo bar = { .arr = { 0xde, 0xad, 0xbe, 0xef } };

int __test() {

    return bar.x;
}

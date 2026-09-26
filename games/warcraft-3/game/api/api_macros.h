#ifndef api_macros_h
#define api_macros_h

typedef int jassdef_integer;
typedef float jassdef_number;
typedef char const *jassdef_string;

#define EVAL0(...) __VA_ARGS__
#define EVAL1(...) EVAL0(EVAL0(EVAL0(__VA_ARGS__)))
#define EVAL2(...) EVAL1(EVAL1(EVAL1(__VA_ARGS__)))
#define EVAL3(...) EVAL2(EVAL2(EVAL2(__VA_ARGS__)))
#define EVAL4(...) EVAL3(EVAL3(EVAL3(__VA_ARGS__)))
#define EVAL(...)  EVAL4(EVAL4(EVAL4(__VA_ARGS__)))

#define MAP_END(...)
#define MAP_OUT
#define MAP_COMMA ,

#define MAP_GET_END2() 0, MAP_END
#define MAP_GET_END1(...) MAP_GET_END2
#define MAP_GET_END(...) MAP_GET_END1
#define MAP_NEXT0(test, next, ...) next MAP_OUT
#define MAP_NEXT1(test, next) MAP_NEXT0(test, next, 0)
#define MAP_NEXT(test, next)  MAP_NEXT1(MAP_GET_END test, next)

#define MAP0(f, x, peek, ...) f(x) MAP_NEXT(peek, MAP1)(f, peek, __VA_ARGS__)
#define MAP1(f, x, peek, ...) f(x) MAP_NEXT(peek, MAP0)(f, peek, __VA_ARGS__)

#define MAP_LIST_NEXT1(test, next) MAP_NEXT0(test, MAP_COMMA next, 0)
#define MAP_LIST_NEXT(test, next)  MAP_LIST_NEXT1(MAP_GET_END test, next)

#define MAP_LIST0(f, x, peek, ...) f(x) MAP_LIST_NEXT(peek, MAP_LIST1)(f, peek, __VA_ARGS__)
#define MAP_LIST1(f, x, peek, ...) f(x) MAP_LIST_NEXT(peek, MAP_LIST0)(f, peek, __VA_ARGS__)

#define MAP(f, ...) EVAL(MAP1(f, __VA_ARGS__, ()()(), ()()(), ()()(), 0))
#define MAP_LIST(f, ...) EVAL(MAP_LIST1(f, __VA_ARGS__, ()()(), ()()(), ()()(), 0))

#define JASS_STACK2(T, V) jassdef_##T V = jass_check##T(j, ++__arg);
#define JASS_STACK3(T, V, N) T *V = jass_checkhandle(j, ++__arg, N);
#define JASS_ARG2(T, V) jassdef_##T V
#define JASS_ARG3(T, V, N) T *V

#define MKFN(fn,...) MKFN_N(fn,##__VA_ARGS__,9,8,7,6,5,4,3,2,1,0)(__VA_ARGS__)
#define MKFN_N(fn,n0,n1,n2,n3,n4,n5,n6,n7,n8,n,...) fn##n
#define JASS_STACK_N(...) MKFN(JASS_STACK,##__VA_ARGS__)
#define JASS_ARG_N(...) MKFN(JASS_ARG,##__VA_ARGS__)
#define JASS_VALUE_N(TYPE, NAME, ...) NAME

#define JASS_STACK(x) JASS_STACK_N x;
#define JASS_ARG(x) JASS_ARG_N x
#define JASS_VALUE(x) JASS_VALUE_N x

#define JASS_API(NAME, ...) \
void NAME##_native(MAP_LIST(JASS_ARG, __VA_ARGS__)); \
uint32_t NAME(jass_t *j) { \
    uint32_t __arg = 0; \
    MAP(JASS_STACK, __VA_ARGS__); \
    NAME##_native(MAP_LIST(JASS_VALUE, __VA_ARGS__)); \
    return 0; \
} \
void NAME##_native(MAP_LIST(JASS_ARG, __VA_ARGS__))

#endif

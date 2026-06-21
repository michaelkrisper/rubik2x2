#ifndef __RUBIK_HPP
#define __RUBIK_HPP

using namespace __shedskin__;
namespace __rubik__ {

extern str *const_0, *const_1, *const_10, *const_11, *const_12, *const_13, *const_14, *const_15, *const_16, *const_17, *const_18, *const_19, *const_2, *const_20, *const_21, *const_22, *const_23, *const_24, *const_25, *const_26, *const_27, *const_28, *const_29, *const_3, *const_30, *const_31, *const_32, *const_33, *const_34, *const_35, *const_36, *const_37, *const_38, *const_39, *const_4, *const_40, *const_41, *const_42, *const_43, *const_44, *const_45, *const_46, *const_47, *const_5, *const_6, *const_7, *const_8, *const_9;


typedef list<list<list<str *> *> *> *(*lambda4)(list<list<list<str *> *> *> *);
typedef list<list<list<str *> *> *> *(*lambda3)(list<list<list<str *> *> *> *);
typedef list<list<list<str *> *> *> *(*lambda6)(list<list<list<str *> *> *> *);
typedef void *(*lambda13)(void *, void *, void *, void *, void *);
typedef list<list<list<str *> *> *> *(*lambda7)(list<list<list<str *> *> *> *);
typedef list<list<list<str *> *> *> *(*lambda9)(list<list<list<str *> *> *> *);
typedef list<list<list<str *> *> *> *(*lambda10)(list<list<list<str *> *> *> *);
typedef void *(*lambda12)(void *, void *, void *, void *, void *);
typedef list<list<list<str *> *> *> *(*lambda5)(list<list<list<str *> *> *> *);
typedef list<list<list<str *> *> *> *(*lambda11)(list<list<list<str *> *> *> *);
typedef list<list<list<str *> *> *> *(*lambda0)(list<list<list<str *> *> *> *);
typedef list<list<list<str *> *> *> *(*lambda2)(list<list<list<str *> *> *> *);
typedef list<list<list<str *> *> *> *(*lambda1)(list<list<list<str *> *> *> *);
typedef list<list<list<str *> *> *> *(*lambda8)(list<list<list<str *> *> *> *);

extern __ss_int BACK, BOTTOM, FRONT, LEFT, LEVEL_OF_PRECALCULATION, RIGHT, TOP;
extern str *B, *G, *P, *R, *W, *Y, *__name__;
extern dict<str *, tuple2<list<list<list<str *> *> *> *, str *> *> *preSolutionCubes;


list<list<list<str *> *> *> *defaultCube();
list<list<list<str *> *> *> *copyCube(list<list<list<str *> *> *> *cube);
void *printcube(list<list<list<str *> *> *> *cube);
void *calculateDefaultCubesOriented(dict<str *, tuple2<list<list<list<str *> *> *> *, str *> *> *cubes);
tuple2<__ss_bool, str *> *checkSolved(list<list<list<str *> *> *> *testCube);
__iter<tuple2<list<list<list<str *> *> *> *, str *> *> *getAllMoves(list<list<list<str *> *> *> *currCube, str *path);
list<list<list<str *> *> *> *leftDown(list<list<list<str *> *> *> *cube);
list<list<list<str *> *> *> *rightDown(list<list<list<str *> *> *> *cube);
list<list<list<str *> *> *> *rightUp(list<list<list<str *> *> *> *cube);
list<list<list<str *> *> *> *leftUp(list<list<list<str *> *> *> *cube);
list<list<list<str *> *> *> *topLeft(list<list<list<str *> *> *> *cube);
list<list<list<str *> *> *> *topRight(list<list<list<str *> *> *> *cube);
list<list<list<str *> *> *> *bottomLeft(list<list<list<str *> *> *> *cube);
list<list<list<str *> *> *> *bottomRight(list<list<list<str *> *> *> *cube);
list<list<list<str *> *> *> *frontClockwise(list<list<list<str *> *> *> *cube);
list<list<list<str *> *> *> *frontAntiClockwise(list<list<list<str *> *> *> *cube);
list<list<list<str *> *> *> *backClockwise(list<list<list<str *> *> *> *cube);
list<list<list<str *> *> *> *backAntiClockwise(list<list<list<str *> *> *> *cube);
list<list<list<str *> *> *> *readCube(str *inputText);
void *solve(list<list<list<str *> *> *> *inputCube);
list<list<list<str *> *> *> *random_moves(list<list<list<str *> *> *> *cube, __ss_int count);
str *get_key(list<list<list<str *> *> *> *cube);
void *preCalcCubes();
void *__ss_main();

} // module namespace
#endif

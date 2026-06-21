#include "builtin.hpp"
#include "datetime.hpp"
#include "string.hpp"
#include "time.hpp"
#include "sys.hpp"
#include "random.hpp"
#include "math.hpp"
#include "rubik.hpp"

/**
Rubik Cube 2x2 Solver. 

Input format:
  Colors: 
    red = r
    white = w
    blue = b
    yellow = y
    purple = p
    green = g

Arguments:
      <Top><Front><Bottom><Back><Left><Right>
      tttt ffff   bbbb    aaaa  llll  rrrr
      e.G. rubik.py rgrggpyybbppwwbrwwgpyyrb
      or:  rubik.py rrwrbwwwpppyygyybrbbgggp  (cube in a cube -- very complex to solve)

    Order of surfaces on one side:
      -------
      |1. 2.|
      |3. 4.|
      -------

Available Commands in the Terminal:
  exit    exit the program
  print   print the current cube
  solve   try to find a solution for the current cube
  init    re-init the cube with a new side sequence
  random  randomize the current cube with 8 moves
  random4 randomize the current cube with 4 moves
  l/L     move LEFT down / LEFT up
  f/F     move FRONT clockwise / FRONT anticlockwise
  t/T     move TOP LEFT / TOP RIGHT

  The move-commands can be chained: e.G. lFtL

  http://www.mathematische-basteleien.de/miniwuerfel.htm
  http://www.jaapsch.net/puzzles/javascript/cube2j.htm  


  Very complex moves:
  ttlfTlFlFTLL cube in a cube
  lfllffTFttlt
  lftfLfLfftLT
  lltfLfLtFLTT

  TODO:
  * Move everything into a class for better reuse
  * document (doc-strings)
  * correct naming (make more intuitive)
  * Use Threads during solve
*/

namespace __rubik__ {

str *const_0, *const_1, *const_10, *const_11, *const_12, *const_13, *const_14, *const_15, *const_16, *const_17, *const_18, *const_19, *const_2, *const_20, *const_21, *const_22, *const_23, *const_24, *const_25, *const_26, *const_27, *const_28, *const_29, *const_3, *const_30, *const_31, *const_32, *const_33, *const_34, *const_35, *const_36, *const_37, *const_38, *const_39, *const_4, *const_40, *const_41, *const_42, *const_43, *const_44, *const_45, *const_46, *const_47, *const_5, *const_6, *const_7, *const_8, *const_9;


dict<str *, tuple2<list<list<list<str *> *> *> *, str *> *> *preSolutionCubes;
str *B, *G, *P, *R, *W, *Y, *__name__;
__ss_int BACK, BOTTOM, FRONT, LEFT, LEVEL_OF_PRECALCULATION, RIGHT, TOP;


static inline list<list<str *> *> *list_comp_0(list<list<str *> *> *side);
static inline list<list<list<str *> *> *> *list_comp_1(list<list<list<str *> *> *> *cube);
static inline list<str *> *list_comp_2(list<list<list<str *> *> *> *cube);

static inline list<list<str *> *> *list_comp_0(list<list<str *> *> *side) {
    __iter<list<str *> *> *__8;
    list<str *> *row;
    list<list<str *> *> *__7;
    __ss_int __9;
    list<list<str *> *>::for_in_loop __10;

    list<list<str *> *> *__ss_result = new list<list<str *> *>();

    __ss_result->resize(len(side));
    FOR_IN(row,side,7,9,10)
        __ss_result->units[__9] = row->__slice__(0, 0, 0, 0);
    END_FOR

    return __ss_result;
}

static inline list<list<list<str *> *> *> *list_comp_1(list<list<list<str *> *> *> *cube) {
    list<list<list<str *> *> *>::for_in_loop __6;
    list<list<list<str *> *> *> *__3;
    __iter<list<list<str *> *> *> *__4;
    __ss_int __5;
    list<list<str *> *> *side;

    list<list<list<str *> *> *> *__ss_result = new list<list<list<str *> *> *>();

    __ss_result->resize(len(cube));
    FOR_IN(side,cube,3,5,6)
        __ss_result->units[__5] = list_comp_0(side);
    END_FOR

    return __ss_result;
}

static inline list<str *> *list_comp_2(list<list<list<str *> *> *> *cube) {
    list<str *> *__231, *row;
    list<list<str *> *>::for_in_loop __230;
    list<str *>::for_in_loop __234;
    __iter<list<list<str *> *> *> *__224;
    __iter<str *> *__232;
    list<list<list<str *> *> *>::for_in_loop __226;
    __iter<list<str *> *> *__228;
    str *pos;
    list<list<list<str *> *> *> *__223;
    list<list<str *> *> *__227, *side;
    __ss_int __225, __229, __233;

    list<str *> *__ss_result = new list<str *>();

    FOR_IN(side,cube,223,225,226)
        FOR_IN(row,side,227,229,230)
            FOR_IN(pos,row,231,233,234)
                __ss_result->append(pos);
            END_FOR

        END_FOR

    END_FOR

    return __ss_result;
}

list<list<list<str *> *> *> *defaultCube() {
    
    return (new list<list<list<str *> *> *>(6,(new list<list<str *> *>(2,(new list<str *>(2,Y,Y)),(new list<str *>(2,Y,Y)))),(new list<list<str *> *>(2,(new list<str *>(2,R,R)),(new list<str *>(2,R,R)))),(new list<list<str *> *>(2,(new list<str *>(2,W,W)),(new list<str *>(2,W,W)))),(new list<list<str *> *>(2,(new list<str *>(2,P,P)),(new list<str *>(2,P,P)))),(new list<list<str *> *>(2,(new list<str *>(2,B,B)),(new list<str *>(2,B,B)))),(new list<list<str *> *>(2,(new list<str *>(2,G,G)),(new list<str *>(2,G,G))))));
}

list<list<list<str *> *> *> *copyCube(list<list<list<str *> *> *> *cube) {
    
    return list_comp_1(cube);
}

void *printcube(list<list<list<str *> *> *> *cube) {
    
    print2(NULL,0,1, const_0);
    print2(NULL,0,1, __modct(const_1, 2, ((cube->__getfast__(TOP))->__getfast__(0))->__getfast__(0), ((cube->__getfast__(TOP))->__getfast__(0))->__getfast__(1)));
    print2(NULL,0,1, const_2);
    print2(NULL,0,1, __modct(const_1, 2, ((cube->__getfast__(TOP))->__getfast__(1))->__getfast__(0), ((cube->__getfast__(TOP))->__getfast__(1))->__getfast__(1)));
    print2(NULL,0,1, const_3);
    print2(NULL,0,1, __modct(const_4, 8, ((cube->__getfast__(LEFT))->__getfast__(0))->__getfast__(0), ((cube->__getfast__(LEFT))->__getfast__(0))->__getfast__(1), ((cube->__getfast__(FRONT))->__getfast__(0))->__getfast__(0), ((cube->__getfast__(FRONT))->__getfast__(0))->__getfast__(1), ((cube->__getfast__(RIGHT))->__getfast__(0))->__getfast__(0), ((cube->__getfast__(RIGHT))->__getfast__(0))->__getfast__(1), ((cube->__getfast__(BACK))->__getfast__(1))->__getfast__(1), ((cube->__getfast__(BACK))->__getfast__(1))->__getfast__(0)));
    print2(NULL,0,1, const_5);
    print2(NULL,0,1, __modct(const_4, 8, ((cube->__getfast__(LEFT))->__getfast__(1))->__getfast__(0), ((cube->__getfast__(LEFT))->__getfast__(1))->__getfast__(1), ((cube->__getfast__(FRONT))->__getfast__(1))->__getfast__(0), ((cube->__getfast__(FRONT))->__getfast__(1))->__getfast__(1), ((cube->__getfast__(RIGHT))->__getfast__(1))->__getfast__(0), ((cube->__getfast__(RIGHT))->__getfast__(1))->__getfast__(1), ((cube->__getfast__(BACK))->__getfast__(0))->__getfast__(1), ((cube->__getfast__(BACK))->__getfast__(0))->__getfast__(0)));
    print2(NULL,0,1, const_6);
    print2(NULL,0,1, __modct(const_1, 2, ((cube->__getfast__(BOTTOM))->__getfast__(0))->__getfast__(0), ((cube->__getfast__(BOTTOM))->__getfast__(0))->__getfast__(1)));
    print2(NULL,0,1, const_2);
    print2(NULL,0,1, __modct(const_1, 2, ((cube->__getfast__(BOTTOM))->__getfast__(1))->__getfast__(0), ((cube->__getfast__(BOTTOM))->__getfast__(1))->__getfast__(1)));
    print2(NULL,0,1, const_7);
    return NULL;
}

void *calculateDefaultCubesOriented(dict<str *, tuple2<list<list<list<str *> *> *> *, str *> *> *cubes) {
    list<list<list<str *> *> *> *c, *ctb, *ctbtb, *ctbtbtb, *cube, *nextCube;
    __iter<list<list<list<str *> *> *> *> *__12;
    list<list<list<list<str *> *> *> *> *__11;
    __ss_int __13;
    list<list<list<list<str *> *> *> *>::for_in_loop __14;

    c = defaultCube();
    ctb = topLeft(bottomLeft(copyCube(c)));
    ctbtb = topLeft(bottomLeft(copyCube(ctb)));
    ctbtbtb = topLeft(bottomLeft(copyCube(ctbtb)));
    cubes->__setitem__(get_key(c), (new tuple2<list<list<list<str *> *> *> *, str *>(2,c,const_8)));
    cubes->__setitem__(get_key(ctb), (new tuple2<list<list<list<str *> *> *> *, str *>(2,ctb,const_8)));
    cubes->__setitem__(get_key(ctbtb), (new tuple2<list<list<list<str *> *> *> *, str *>(2,ctbtb,const_8)));
    cubes->__setitem__(get_key(ctbtbtb), (new tuple2<list<list<list<str *> *> *> *, str *>(2,ctbtbtb,const_8)));

    FOR_IN(cube,(new list<list<list<list<str *> *> *> *>(4,c,ctb,ctbtb,ctbtbtb)),11,13,14)
        nextCube = cube;
        nextCube = rightDown(leftDown(copyCube(nextCube)));
        cubes->__setitem__(get_key(nextCube), (new tuple2<list<list<list<str *> *> *> *, str *>(2,nextCube,const_8)));
        nextCube = rightDown(leftDown(copyCube(nextCube)));
        cubes->__setitem__(get_key(nextCube), (new tuple2<list<list<list<str *> *> *> *, str *>(2,nextCube,const_8)));
        nextCube = rightDown(leftDown(copyCube(nextCube)));
        cubes->__setitem__(get_key(nextCube), (new tuple2<list<list<list<str *> *> *> *, str *>(2,nextCube,const_8)));
        nextCube = cube;
        nextCube = frontClockwise(backClockwise(copyCube(nextCube)));
        cubes->__setitem__(get_key(nextCube), (new tuple2<list<list<list<str *> *> *> *, str *>(2,nextCube,const_8)));
        nextCube = frontClockwise(backClockwise(copyCube(nextCube)));
        cubes->__setitem__(get_key(nextCube), (new tuple2<list<list<list<str *> *> *> *, str *>(2,nextCube,const_8)));
        nextCube = frontClockwise(backClockwise(copyCube(nextCube)));
        cubes->__setitem__(get_key(nextCube), (new tuple2<list<list<list<str *> *> *> *, str *>(2,nextCube,const_8)));
    END_FOR

    return NULL;
}

tuple2<__ss_bool, str *> *checkSolved(list<list<list<str *> *> *> *testCube) {
    str *key, *path;

    key = get_key(testCube);
    if ((preSolutionCubes)->__contains__(key)) {
        path = preSolutionCubes->__getitem__(key)->__getsecond__();
        return (new tuple2<__ss_bool, str *>(2,True,(path->swapcase())->__slice__(4, 0, 0, (-1))));
    }
    else {
        return (new tuple2<__ss_bool, str *>(2,False,const_8));
    }
    return 0;
}

class __gen_getAllMoves : public __iter<tuple2<list<list<list<str *> *> *> *, str *> *> {
public:
    list<list<list<str *> *> *> *currCube;
    str *path;
    __ss_bool __15, __16, __17, __18, __19, __20, __21, __22, __23, __24, __25, __26, __27, __28, __29, __30, __31, __32, __33, __34, __35, __36, __37, __38, __39, __40, __41, __42, __43, __44, __45, __46, __47, __48, __49, __50;

    int __last_yield;

    __gen_getAllMoves(list<list<list<str *> *> *> *currCube,str *path) {
        this->currCube = currCube;
        this->path = path;
        __last_yield = -1;
    }

    tuple2<list<list<list<str *> *> *> *, str *> * __get_next() {
        switch(__last_yield) {
            case 0: goto __after_yield_0;
            case 1: goto __after_yield_1;
            case 2: goto __after_yield_2;
            case 3: goto __after_yield_3;
            case 4: goto __after_yield_4;
            case 5: goto __after_yield_5;
            default: break;
        }
        if ((((len(path)==0) or __ne(path->__getfast__((-1)), const_9)) and ((len(path)<3) or __ne(path->__slice__(1, (-3), 0, 0), const_10)))) {
            __last_yield = 0;
            __result = (new tuple2<list<list<list<str *> *> *> *, str *>(2,leftDown(copyCube(currCube)),(path)->__add__(const_11)));
            return __result;
            __after_yield_0:;
        }
        if ((((len(path)==0) or __ne(path->__getfast__((-1)), const_12)) and ((len(path)<3) or __ne(path->__slice__(1, (-3), 0, 0), const_13)))) {
            __last_yield = 1;
            __result = (new tuple2<list<list<list<str *> *> *> *, str *>(2,topLeft(copyCube(currCube)),(path)->__add__(const_14)));
            return __result;
            __after_yield_1:;
        }
        if ((((len(path)==0) or __ne(path->__getfast__((-1)), const_15)) and ((len(path)<3) or __ne(path->__slice__(1, (-3), 0, 0), const_16)))) {
            __last_yield = 2;
            __result = (new tuple2<list<list<list<str *> *> *> *, str *>(2,frontClockwise(copyCube(currCube)),(path)->__add__(const_17)));
            return __result;
            __after_yield_2:;
        }
        if ((((len(path)==0) or __ne(path->__getfast__((-1)), const_11)) and ((len(path)<3) or __ne(path->__slice__(1, (-3), 0, 0), const_18)))) {
            __last_yield = 3;
            __result = (new tuple2<list<list<list<str *> *> *> *, str *>(2,leftUp(copyCube(currCube)),(path)->__add__(const_9)));
            return __result;
            __after_yield_3:;
        }
        if ((((len(path)==0) or __ne(path->__getfast__((-1)), const_14)) and ((len(path)<3) or __ne(path->__slice__(1, (-3), 0, 0), const_19)))) {
            __last_yield = 4;
            __result = (new tuple2<list<list<list<str *> *> *> *, str *>(2,topRight(copyCube(currCube)),(path)->__add__(const_12)));
            return __result;
            __after_yield_4:;
        }
        if ((((len(path)==0) or __ne(path->__getfast__((-1)), const_17)) and ((len(path)<3) or __ne(path->__slice__(1, (-3), 0, 0), const_20)))) {
            __last_yield = 5;
            __result = (new tuple2<list<list<list<str *> *> *> *, str *>(2,frontAntiClockwise(copyCube(currCube)),(path)->__add__(const_15)));
            return __result;
            __after_yield_5:;
        }
        __stop_iteration = true;
    }

};

__iter<tuple2<list<list<list<str *> *> *> *, str *> *> *getAllMoves(list<list<list<str *> *> *> *currCube, str *path) {
    return new __gen_getAllMoves(currCube,path);

}

list<list<list<str *> *> *> *leftDown(list<list<list<str *> *> *> *cube) {
    list<str *> *__51, *__52, *__53, *__54, *__55, *__56, *__57, *__58, *__59, *__60, *__61, *__62;
    str *__63, *__64, *__65, *__66, *__67, *__68, *__69, *__70, *__71, *__72, *__73, *__74;

    __63 = ((cube->__getfast__(BACK))->__getfast__(0))->__getfast__(0);
    __64 = ((cube->__getfast__(BACK))->__getfast__(1))->__getfast__(0);
    __65 = ((cube->__getfast__(TOP))->__getfast__(0))->__getfast__(0);
    __66 = ((cube->__getfast__(TOP))->__getfast__(1))->__getfast__(0);
    __67 = ((cube->__getfast__(FRONT))->__getfast__(0))->__getfast__(0);
    __68 = ((cube->__getfast__(FRONT))->__getfast__(1))->__getfast__(0);
    __69 = ((cube->__getfast__(BOTTOM))->__getfast__(0))->__getfast__(0);
    __70 = ((cube->__getfast__(BOTTOM))->__getfast__(1))->__getfast__(0);
    __71 = ((cube->__getfast__(LEFT))->__getfast__(1))->__getfast__(0);
    __72 = ((cube->__getfast__(LEFT))->__getfast__(0))->__getfast__(0);
    __73 = ((cube->__getfast__(LEFT))->__getfast__(1))->__getfast__(1);
    __74 = ((cube->__getfast__(LEFT))->__getfast__(0))->__getfast__(1);
    (cube->__getfast__(TOP))->__getfast__(0)->__setitem__(0, __63);
    (cube->__getfast__(TOP))->__getfast__(1)->__setitem__(0, __64);
    (cube->__getfast__(FRONT))->__getfast__(0)->__setitem__(0, __65);
    (cube->__getfast__(FRONT))->__getfast__(1)->__setitem__(0, __66);
    (cube->__getfast__(BOTTOM))->__getfast__(0)->__setitem__(0, __67);
    (cube->__getfast__(BOTTOM))->__getfast__(1)->__setitem__(0, __68);
    (cube->__getfast__(BACK))->__getfast__(0)->__setitem__(0, __69);
    (cube->__getfast__(BACK))->__getfast__(1)->__setitem__(0, __70);
    (cube->__getfast__(LEFT))->__getfast__(0)->__setitem__(0, __71);
    (cube->__getfast__(LEFT))->__getfast__(0)->__setitem__(1, __72);
    (cube->__getfast__(LEFT))->__getfast__(1)->__setitem__(0, __73);
    (cube->__getfast__(LEFT))->__getfast__(1)->__setitem__(1, __74);
    return cube;
}

list<list<list<str *> *> *> *rightDown(list<list<list<str *> *> *> *cube) {
    list<str *> *__75, *__76, *__77, *__78, *__79, *__80, *__81, *__82, *__83, *__84, *__85, *__86;
    str *__87, *__88, *__89, *__90, *__91, *__92, *__93, *__94, *__95, *__96, *__97, *__98;

    __87 = ((cube->__getfast__(BACK))->__getfast__(0))->__getfast__(1);
    __88 = ((cube->__getfast__(BACK))->__getfast__(1))->__getfast__(1);
    __89 = ((cube->__getfast__(TOP))->__getfast__(0))->__getfast__(1);
    __90 = ((cube->__getfast__(TOP))->__getfast__(1))->__getfast__(1);
    __91 = ((cube->__getfast__(FRONT))->__getfast__(0))->__getfast__(1);
    __92 = ((cube->__getfast__(FRONT))->__getfast__(1))->__getfast__(1);
    __93 = ((cube->__getfast__(BOTTOM))->__getfast__(0))->__getfast__(1);
    __94 = ((cube->__getfast__(BOTTOM))->__getfast__(1))->__getfast__(1);
    __95 = ((cube->__getfast__(RIGHT))->__getfast__(0))->__getfast__(1);
    __96 = ((cube->__getfast__(RIGHT))->__getfast__(1))->__getfast__(1);
    __97 = ((cube->__getfast__(RIGHT))->__getfast__(0))->__getfast__(0);
    __98 = ((cube->__getfast__(RIGHT))->__getfast__(1))->__getfast__(0);
    (cube->__getfast__(TOP))->__getfast__(0)->__setitem__(1, __87);
    (cube->__getfast__(TOP))->__getfast__(1)->__setitem__(1, __88);
    (cube->__getfast__(FRONT))->__getfast__(0)->__setitem__(1, __89);
    (cube->__getfast__(FRONT))->__getfast__(1)->__setitem__(1, __90);
    (cube->__getfast__(BOTTOM))->__getfast__(0)->__setitem__(1, __91);
    (cube->__getfast__(BOTTOM))->__getfast__(1)->__setitem__(1, __92);
    (cube->__getfast__(BACK))->__getfast__(0)->__setitem__(1, __93);
    (cube->__getfast__(BACK))->__getfast__(1)->__setitem__(1, __94);
    (cube->__getfast__(RIGHT))->__getfast__(0)->__setitem__(0, __95);
    (cube->__getfast__(RIGHT))->__getfast__(0)->__setitem__(1, __96);
    (cube->__getfast__(RIGHT))->__getfast__(1)->__setitem__(0, __97);
    (cube->__getfast__(RIGHT))->__getfast__(1)->__setitem__(1, __98);
    return cube;
}

list<list<list<str *> *> *> *rightUp(list<list<list<str *> *> *> *cube) {
    
    rightDown(cube);
    rightDown(cube);
    rightDown(cube);
    return cube;
}

list<list<list<str *> *> *> *leftUp(list<list<list<str *> *> *> *cube) {
    
    leftDown(cube);
    leftDown(cube);
    leftDown(cube);
    return cube;
}

list<list<list<str *> *> *> *topLeft(list<list<list<str *> *> *> *cube) {
    list<str *> *__100, *__101, *__102, *__103, *__104, *__105, *__106, *__107, *__108, *__109, *__110, *__99;
    str *__111, *__112, *__113, *__114, *__115, *__116, *__117, *__118, *__119, *__120, *__121, *__122;

    __111 = ((cube->__getfast__(RIGHT))->__getfast__(0))->__getfast__(0);
    __112 = ((cube->__getfast__(RIGHT))->__getfast__(0))->__getfast__(1);
    __113 = ((cube->__getfast__(FRONT))->__getfast__(0))->__getfast__(0);
    __114 = ((cube->__getfast__(FRONT))->__getfast__(0))->__getfast__(1);
    __115 = ((cube->__getfast__(LEFT))->__getfast__(0))->__getfast__(1);
    __116 = ((cube->__getfast__(LEFT))->__getfast__(0))->__getfast__(0);
    __117 = ((cube->__getfast__(BACK))->__getfast__(1))->__getfast__(1);
    __118 = ((cube->__getfast__(BACK))->__getfast__(1))->__getfast__(0);
    __119 = ((cube->__getfast__(TOP))->__getfast__(1))->__getfast__(0);
    __120 = ((cube->__getfast__(TOP))->__getfast__(0))->__getfast__(0);
    __121 = ((cube->__getfast__(TOP))->__getfast__(1))->__getfast__(1);
    __122 = ((cube->__getfast__(TOP))->__getfast__(0))->__getfast__(1);
    (cube->__getfast__(FRONT))->__getfast__(0)->__setitem__(0, __111);
    (cube->__getfast__(FRONT))->__getfast__(0)->__setitem__(1, __112);
    (cube->__getfast__(LEFT))->__getfast__(0)->__setitem__(0, __113);
    (cube->__getfast__(LEFT))->__getfast__(0)->__setitem__(1, __114);
    (cube->__getfast__(BACK))->__getfast__(1)->__setitem__(0, __115);
    (cube->__getfast__(BACK))->__getfast__(1)->__setitem__(1, __116);
    (cube->__getfast__(RIGHT))->__getfast__(0)->__setitem__(0, __117);
    (cube->__getfast__(RIGHT))->__getfast__(0)->__setitem__(1, __118);
    (cube->__getfast__(TOP))->__getfast__(0)->__setitem__(0, __119);
    (cube->__getfast__(TOP))->__getfast__(0)->__setitem__(1, __120);
    (cube->__getfast__(TOP))->__getfast__(1)->__setitem__(0, __121);
    (cube->__getfast__(TOP))->__getfast__(1)->__setitem__(1, __122);
    return cube;
}

list<list<list<str *> *> *> *topRight(list<list<list<str *> *> *> *cube) {
    
    topLeft(cube);
    topLeft(cube);
    topLeft(cube);
    return cube;
}

list<list<list<str *> *> *> *bottomLeft(list<list<list<str *> *> *> *cube) {
    list<str *> *__123, *__124, *__125, *__126, *__127, *__128, *__129, *__130, *__131, *__132, *__133, *__134;
    str *__135, *__136, *__137, *__138, *__139, *__140, *__141, *__142, *__143, *__144, *__145, *__146;

    __135 = ((cube->__getfast__(RIGHT))->__getfast__(1))->__getfast__(0);
    __136 = ((cube->__getfast__(RIGHT))->__getfast__(1))->__getfast__(1);
    __137 = ((cube->__getfast__(FRONT))->__getfast__(1))->__getfast__(0);
    __138 = ((cube->__getfast__(FRONT))->__getfast__(1))->__getfast__(1);
    __139 = ((cube->__getfast__(LEFT))->__getfast__(1))->__getfast__(1);
    __140 = ((cube->__getfast__(LEFT))->__getfast__(1))->__getfast__(0);
    __141 = ((cube->__getfast__(BACK))->__getfast__(0))->__getfast__(1);
    __142 = ((cube->__getfast__(BACK))->__getfast__(0))->__getfast__(0);
    __143 = ((cube->__getfast__(BOTTOM))->__getfast__(0))->__getfast__(1);
    __144 = ((cube->__getfast__(BOTTOM))->__getfast__(1))->__getfast__(1);
    __145 = ((cube->__getfast__(BOTTOM))->__getfast__(0))->__getfast__(0);
    __146 = ((cube->__getfast__(BOTTOM))->__getfast__(1))->__getfast__(0);
    (cube->__getfast__(FRONT))->__getfast__(1)->__setitem__(0, __135);
    (cube->__getfast__(FRONT))->__getfast__(1)->__setitem__(1, __136);
    (cube->__getfast__(LEFT))->__getfast__(1)->__setitem__(0, __137);
    (cube->__getfast__(LEFT))->__getfast__(1)->__setitem__(1, __138);
    (cube->__getfast__(BACK))->__getfast__(0)->__setitem__(0, __139);
    (cube->__getfast__(BACK))->__getfast__(0)->__setitem__(1, __140);
    (cube->__getfast__(RIGHT))->__getfast__(1)->__setitem__(0, __141);
    (cube->__getfast__(RIGHT))->__getfast__(1)->__setitem__(1, __142);
    (cube->__getfast__(BOTTOM))->__getfast__(0)->__setitem__(0, __143);
    (cube->__getfast__(BOTTOM))->__getfast__(0)->__setitem__(1, __144);
    (cube->__getfast__(BOTTOM))->__getfast__(1)->__setitem__(0, __145);
    (cube->__getfast__(BOTTOM))->__getfast__(1)->__setitem__(1, __146);
    return cube;
}

list<list<list<str *> *> *> *bottomRight(list<list<list<str *> *> *> *cube) {
    
    bottomLeft(cube);
    bottomLeft(cube);
    bottomLeft(cube);
    return cube;
}

list<list<list<str *> *> *> *frontClockwise(list<list<list<str *> *> *> *cube) {
    list<str *> *__147, *__148, *__149, *__150, *__151, *__152, *__153, *__154, *__155, *__156, *__157, *__158;
    str *__159, *__160, *__161, *__162, *__163, *__164, *__165, *__166, *__167, *__168, *__169, *__170;

    __159 = ((cube->__getfast__(LEFT))->__getfast__(1))->__getfast__(1);
    __160 = ((cube->__getfast__(LEFT))->__getfast__(0))->__getfast__(1);
    __161 = ((cube->__getfast__(BOTTOM))->__getfast__(0))->__getfast__(0);
    __162 = ((cube->__getfast__(BOTTOM))->__getfast__(0))->__getfast__(1);
    __163 = ((cube->__getfast__(RIGHT))->__getfast__(1))->__getfast__(0);
    __164 = ((cube->__getfast__(RIGHT))->__getfast__(0))->__getfast__(0);
    __165 = ((cube->__getfast__(TOP))->__getfast__(1))->__getfast__(0);
    __166 = ((cube->__getfast__(TOP))->__getfast__(1))->__getfast__(1);
    __167 = ((cube->__getfast__(FRONT))->__getfast__(1))->__getfast__(0);
    __168 = ((cube->__getfast__(FRONT))->__getfast__(0))->__getfast__(0);
    __169 = ((cube->__getfast__(FRONT))->__getfast__(1))->__getfast__(1);
    __170 = ((cube->__getfast__(FRONT))->__getfast__(0))->__getfast__(1);
    (cube->__getfast__(TOP))->__getfast__(1)->__setitem__(0, __159);
    (cube->__getfast__(TOP))->__getfast__(1)->__setitem__(1, __160);
    (cube->__getfast__(LEFT))->__getfast__(0)->__setitem__(1, __161);
    (cube->__getfast__(LEFT))->__getfast__(1)->__setitem__(1, __162);
    (cube->__getfast__(BOTTOM))->__getfast__(0)->__setitem__(0, __163);
    (cube->__getfast__(BOTTOM))->__getfast__(0)->__setitem__(1, __164);
    (cube->__getfast__(RIGHT))->__getfast__(0)->__setitem__(0, __165);
    (cube->__getfast__(RIGHT))->__getfast__(1)->__setitem__(0, __166);
    (cube->__getfast__(FRONT))->__getfast__(0)->__setitem__(0, __167);
    (cube->__getfast__(FRONT))->__getfast__(0)->__setitem__(1, __168);
    (cube->__getfast__(FRONT))->__getfast__(1)->__setitem__(0, __169);
    (cube->__getfast__(FRONT))->__getfast__(1)->__setitem__(1, __170);
    return cube;
}

list<list<list<str *> *> *> *frontAntiClockwise(list<list<list<str *> *> *> *cube) {
    
    frontClockwise(cube);
    frontClockwise(cube);
    frontClockwise(cube);
    return cube;
}

list<list<list<str *> *> *> *backClockwise(list<list<list<str *> *> *> *cube) {
    list<str *> *__171, *__172, *__173, *__174, *__175, *__176, *__177, *__178, *__179, *__180, *__181, *__182;
    str *__183, *__184, *__185, *__186, *__187, *__188, *__189, *__190, *__191, *__192, *__193, *__194;

    __183 = ((cube->__getfast__(LEFT))->__getfast__(1))->__getfast__(0);
    __184 = ((cube->__getfast__(LEFT))->__getfast__(0))->__getfast__(0);
    __185 = ((cube->__getfast__(BOTTOM))->__getfast__(1))->__getfast__(0);
    __186 = ((cube->__getfast__(BOTTOM))->__getfast__(1))->__getfast__(1);
    __187 = ((cube->__getfast__(RIGHT))->__getfast__(1))->__getfast__(1);
    __188 = ((cube->__getfast__(RIGHT))->__getfast__(0))->__getfast__(1);
    __189 = ((cube->__getfast__(TOP))->__getfast__(0))->__getfast__(0);
    __190 = ((cube->__getfast__(TOP))->__getfast__(0))->__getfast__(1);
    __191 = ((cube->__getfast__(BACK))->__getfast__(0))->__getfast__(1);
    __192 = ((cube->__getfast__(BACK))->__getfast__(1))->__getfast__(1);
    __193 = ((cube->__getfast__(BACK))->__getfast__(0))->__getfast__(0);
    __194 = ((cube->__getfast__(BACK))->__getfast__(1))->__getfast__(0);
    (cube->__getfast__(TOP))->__getfast__(0)->__setitem__(0, __183);
    (cube->__getfast__(TOP))->__getfast__(0)->__setitem__(1, __184);
    (cube->__getfast__(LEFT))->__getfast__(0)->__setitem__(0, __185);
    (cube->__getfast__(LEFT))->__getfast__(1)->__setitem__(0, __186);
    (cube->__getfast__(BOTTOM))->__getfast__(1)->__setitem__(0, __187);
    (cube->__getfast__(BOTTOM))->__getfast__(1)->__setitem__(1, __188);
    (cube->__getfast__(RIGHT))->__getfast__(0)->__setitem__(1, __189);
    (cube->__getfast__(RIGHT))->__getfast__(1)->__setitem__(1, __190);
    (cube->__getfast__(BACK))->__getfast__(0)->__setitem__(0, __191);
    (cube->__getfast__(BACK))->__getfast__(0)->__setitem__(1, __192);
    (cube->__getfast__(BACK))->__getfast__(1)->__setitem__(0, __193);
    (cube->__getfast__(BACK))->__getfast__(1)->__setitem__(1, __194);
    return cube;
}

list<list<list<str *> *> *> *backAntiClockwise(list<list<list<str *> *> *> *cube) {
    
    backClockwise(cube);
    backClockwise(cube);
    backClockwise(cube);
    return cube;
}

list<list<list<str *> *> *> *readCube(str *inputText) {
    tuple2<__ss_int, __ss_int> *__195;
    __iter<__ss_int> *__196, *__200, *__204;
    list<__ss_int>::for_in_loop __202, __206;
    list<list<list<str *> *> *> *inputCube;
    list<__ss_int> *__199, *__203;
    list<str *> *__207;
    tuple2<__ss_int, __ss_int>::for_in_loop __198;
    __ss_int __197, __201, __205, charPosition, pos, row, side;

    inputCube = defaultCube();
    charPosition = 0;

    FOR_IN(side,(new tuple2<__ss_int, __ss_int>(6,TOP,FRONT,BOTTOM,BACK,LEFT,RIGHT)),195,197,198)

        FOR_IN(row,(new list<__ss_int>(2,0,1)),199,201,202)

            FOR_IN(pos,(new list<__ss_int>(2,0,1)),203,205,206)
                (inputCube->__getfast__(side))->__getfast__(row)->__setitem__(pos, ((str *)));
                charPosition = (charPosition+1);
            END_FOR

        END_FOR

    END_FOR

    return inputCube;
}

void *solve(list<list<list<str *> *> *> *inputCube) {
    __iter<tuple2<list<list<list<str *> *> *> *, str *> *>::for_in_loop __220;
    list<tuple2<list<list<list<str *> *> *> *, str *> *>::for_in_loop __215;
    __iter<tuple2<list<list<list<str *> *> *> *, str *> *> *__213, *__217, *__218;
    __ss_bool solution_found, solved;
    tuple2<list<list<list<str *> *> *> *, str *> *__211, *__216;
    tuple2<__ss_bool, str *> *__208, *__221;
    str *key, *operation, *path, *solvePath;
    list<tuple2<list<list<list<str *> *> *> *, str *> *> *__209, *__210, *__212, *cubes, *newcubes;
    list<list<list<str *> *> *> *currCube, *nextCube;
    set<str *> *cubeKeys;
    __ss_int __214, __219;

    __208 = checkSolved(inputCube);
    solved = __208->__getfirst__();
    solvePath = __208->__getsecond__();
    if (solved) {
        if (__eq(solvePath, const_8)) {
            print2(NULL,0,1, const_21);
        }
        else {
            print2(NULL,0,1, __modct(const_22, 1, solvePath));
        }
    }
    else {
        cubeKeys = (new set<str *>((new list<str *>(1,get_key(inputCube)))));
        newcubes = (new list<tuple2<list<list<list<str *> *> *> *, str *> *>(1,(new tuple2<list<list<list<str *> *> *> *, str *>(2,inputCube,const_8))));
        solution_found = False;
        try {

            while (__NOT(solution_found)) {
                __209 = newcubes;
                __210 = (new list<tuple2<list<list<list<str *> *> *> *, str *> *>());
                cubes = __209;
                newcubes = __210;

                FOR_IN(__211,cubes,212,214,215)
                    __211 = __211;
                    currCube = __211->__getfirst__();
                    path = __211->__getsecond__();

                    FOR_IN(__216,getAllMoves(currCube, path),217,219,220)
                        __216 = __216;
                        nextCube = __216->__getfirst__();
                        operation = __216->__getsecond__();
                        key = get_key(nextCube);
                        if ((!(cubeKeys)->__contains__(key))) {
                            __221 = checkSolved(nextCube);
                            solved = __221->__getfirst__();
                            solvePath = __221->__getsecond__();
                            if (solved) {
                                print2(NULL,0,1, __modct(const_23, 2, operation, solvePath));
                                solution_found = True;
                                return NULL;
                            }
                            cubeKeys->add(key);
                            newcubes->append((new tuple2<list<list<list<str *> *> *> *, str *>(2,nextCube,operation)));
                        }
                    END_FOR

                END_FOR

            }
        } catch (KeyboardInterrupt *) {
        }
    }
    return 0;
}

list<list<list<str *> *> *> *random_moves(list<list<list<str *> *> *> *cube, __ss_int count) {
    set<str *> *cubeKeys;
    str *key;
    __ss_int randomMoves;
    list<lambda2> *moves;
    lambda2 move;

    cubeKeys = (new set<str *>((new list<str *>(1,get_key(cube)))));
    randomMoves = 0;
    moves = (new list<lambda2>(12,((lambda2)(leftDown)),((lambda2)(leftUp)),rightDown,((lambda2)(rightUp)),((lambda2)(topLeft)),((lambda2)(topRight)),((lambda2)(bottomLeft)),((lambda2)(bottomRight)),((lambda2)(frontClockwise)),((lambda2)(frontAntiClockwise)),((lambda2)(backClockwise)),((lambda2)(backAntiClockwise))));

    while ((randomMoves<count)) {
        move = __random__::choice(moves);
        move(cube);
        key = get_key(cube);
        if ((!(cubeKeys)->__contains__(key))) {
            cubeKeys->add(key);
            randomMoves = (randomMoves+1);
        }
    }
    return cube;
}

str *get_key(list<list<list<str *> *> *> *cube) {
    
    return (const_8)->join(list_comp_2(cube));
}

void *preCalcCubes() {
    list<tuple2<str *, tuple2<list<list<list<str *> *> *> *, str *> *> *> *__239;
    __iter<tuple2<list<list<list<str *> *> *> *, str *> *> *__244, *__245;
    tuple2<list<list<list<str *> *> *> *, str *> *__238, *__243;
    __iter<tuple2<list<list<list<str *> *> *> *, str *> *>::for_in_loop __247;
    list<tuple2<str *, tuple2<list<list<list<str *> *> *> *, str *> *> *>::for_in_loop __242;
    str *key, *nextPath, *path;
    list<list<list<str *> *> *> *cube, *nextCube;
    __iter<tuple2<str *, tuple2<list<list<list<str *> *> *> *, str *> *> *> *__240;
    __ss_int _, __235, __236, __241, __246;
    tuple2<str *, tuple2<list<list<list<str *> *> *> *, str *> *> *__237;

    calculateDefaultCubesOriented(preSolutionCubes);

    FAST_FOR(_,0,LEVEL_OF_PRECALCULATION,1,235,236)

        FOR_IN(__237,(preSolutionCubes->items())->__slice__(0, 0, 0, 0),239,241,242)
            __237 = __237;
            key = __237->__getfirst__();
            __238 = __237->__getsecond__();
            cube = __238->__getfirst__();
            path = __238->__getsecond__();

            FOR_IN(__243,getAllMoves(cube, path),244,246,247)
                __243 = __243;
                nextCube = __243->__getfirst__();
                nextPath = __243->__getsecond__();
                key = get_key(nextCube);
                if ((!(preSolutionCubes)->__contains__(key))) {
                    preSolutionCubes->__setitem__(key, (new tuple2<list<list<list<str *> *> *> *, str *>(2,nextCube,nextPath)));
                }
            END_FOR

        END_FOR

    END_FOR

    return NULL;
}

void *__ss_main() {
    __ss_bool __248, __249;
    str *__250, *cmdChar, *cmdText;
    str::for_in_loop __253;
    __iter<str *> *__251;
    __datetime__::datetime *start;
    list<list<list<str *> *> *> *cube;
    __ss_int __252;

    start = __datetime__::datetime::now(NULL);
    preCalcCubes();
    print2(NULL,0,2, const_24, (__datetime__::datetime::now(NULL))->__sub__(start));
    print2(NULL,0,1, const_25);
    if (___bool((__sys__::argv)->__slice__(1, 1, 0, 0))) {
        cube = readCube((__sys__::argv)->__getfast__(1));
    }
    else {
        cube = readCube(const_26);
    }
    printcube(cube);

    while (True) {
        cmdText = raw_input(const_27);
        if (__eq(cmdText, const_28)) {
            solve(cube);
        }
        else if (__eq(cmdText, const_29)) {
            printcube(cube);
        }
        else if (__eq(cmdText, const_30)) {
            random_moves(cube, 8);
            printcube(cube);
        }
        else if (__eq(cmdText, const_31)) {
            random_moves(cube, 4);
            printcube(cube);
        }
        else if (__eq(cmdText, const_32)) {
            cube = defaultCube();
            printcube(cube);
        }
        else if (__eq(cmdText->__slice__(2, 0, 5, 0), const_33)) {
            cube = readCube(cmdText->__slice__(1, 5, 0, 0));
            printcube(cube);
        }
        else if (__eq(cmdText, const_34)) {
            __ss_exit(0);
        }
        else if ((__ne(cmdText, const_8) and ((new set<str *>(cmdText)))->issubset((new set<str *>(const_35))))) {

            FOR_IN(cmdChar,cmdText,250,252,253)
                if (__eq(cmdChar, const_11)) {
                    leftDown(cube);
                }
                else if (__eq(cmdChar, const_14)) {
                    topLeft(cube);
                }
                else if (__eq(cmdChar, const_17)) {
                    frontClockwise(cube);
                }
                else if (__eq(cmdChar, const_9)) {
                    leftUp(cube);
                }
                else if (__eq(cmdChar, const_12)) {
                    topRight(cube);
                }
                else if (__eq(cmdChar, const_15)) {
                    frontAntiClockwise(cube);
                }
                else if (__eq(cmdChar, const_36)) {
                    rightDown(cube);
                }
                else if (__eq(cmdChar, const_36)) {
                    rightUp(cube);
                }
                else if (__eq(cmdChar, const_37)) {
                    bottomLeft(cube);
                }
                else if (__eq(cmdChar, const_37)) {
                    bottomRight(cube);
                }
                else if (__eq(cmdChar, const_38)) {
                    backClockwise(cube);
                }
                else if (__eq(cmdChar, const_39)) {
                    backAntiClockwise(cube);
                }
            END_FOR

            printcube(cube);
        }
        else {
            print2(NULL,0,1, const_40);
        }
    }
    return NULL;
}

void __init() {
    const_0 = new str("        \342\225\224\342\225\220\342\225\220\342\225\220\342\225\244\342\225\220\342\225\220\342\225\220\342\225\227");
    const_1 = new str("        \342\225\221%s\342\224\202%s\342\225\221");
    const_2 = new str("        \342\225\237\342\224\200\342\224\200\342\224\200\342\224\274\342\224\200\342\224\200\342\224\200\342\225\242");
    const_3 = new str("\342\225\224\342\225\220\342\225\220\342\225\220\342\225\244\342\225\220\342\225\220\342\225\220\342\225\254\342\225\220\342\225\220\342\225\220\342\225\252\342\225\220\342\225\220\342\225\220\342\225\254\342\225\220\342\225\220\342\225\220\342\225\244\342\225\220\342\225\220\342\225\220\342\225\246\342\225\220\342\225\220\342\225\220\342\225\244\342\225\220\342\225\220\342\225\220\342\225\227");
    const_4 = new str("\342\225\221%s\342\224\202%s\342\225\221%s\342\224\202%s\342\225\221%s\342\224\202%s\342\225\221%s\342\224\202%s\342\225\221");
    const_5 = new str("\342\225\253\342\224\200\342\224\200\342\224\200\342\224\274\342\224\200\342\224\200\342\224\200\342\225\253\342\224\200\342\224\200\342\224\200\342\224\274\342\224\200\342\224\200\342\224\200\342\225\253\342\224\200\342\224\200\342\224\200\342\224\274\342\224\200\342\224\200\342\224\200\342\225\253\342\224\200\342\224\200\342\224\200\342\224\274\342\224\200\342\224\200\342\224\200\342\225\242");
    const_6 = new str("\342\225\232\342\225\220\342\225\220\342\225\220\342\225\247\342\225\220\342\225\220\342\225\220\342\225\254\342\225\220\342\225\220\342\225\220\342\225\252\342\225\220\342\225\220\342\225\220\342\225\254\342\225\220\342\225\220\342\225\220\342\225\247\342\225\220\342\225\220\342\225\220\342\225\251\342\225\220\342\225\220\342\225\220\342\225\247\342\225\220\342\225\220\342\225\220\342\225\235");
    const_7 = new str("        \342\225\232\342\225\220\342\225\220\342\225\220\342\225\247\342\225\220\342\225\220\342\225\220\342\225\235");
    const_8 = new str("");
    const_9 = __char_cache[76];;
    const_10 = new str("lll");
    const_11 = __char_cache[108];;
    const_12 = __char_cache[84];;
    const_13 = new str("ttt");
    const_14 = __char_cache[116];;
    const_15 = __char_cache[70];;
    const_16 = new str("fff");
    const_17 = __char_cache[102];;
    const_18 = new str("LLL");
    const_19 = new str("TTT");
    const_20 = new str("FFF");
    const_21 = new str("\nThe Cube is already solved. Nothing to do.\n");
    const_22 = new str("\nFound Solution: %s\n");
    const_23 = new str("\nFound Solution: %s%s\n");
    const_24 = new str("time: ");
    const_25 = new str("-- Rubik 2x2 Solver --");
    const_26 = new str("yyyyrrrrwwwwppppbbbbgggg");
    const_27 = new str("Please enter command: ");
    const_28 = new str("solve");
    const_29 = new str("print");
    const_30 = new str("random");
    const_31 = new str("random4");
    const_32 = new str("reset");
    const_33 = new str("init ");
    const_34 = new str("exit");
    const_35 = new str("lLtTfFrRbBaA");
    const_36 = __char_cache[82];;
    const_37 = __char_cache[66];;
    const_38 = __char_cache[97];;
    const_39 = __char_cache[65];;
    const_40 = new str("Command not known. Possible commands are: solve, print, l, L, t, T, f, F, R, R, B, B, a, A, exit");
    const_41 = new str("\033[1;31;41m R \033[0m");
    const_42 = new str("\033[1;37;47m W \033[0m");
    const_43 = new str("\033[1;34;44m B \033[0m");
    const_44 = new str("\033[1;33;43m Y \033[0m");
    const_45 = new str("\033[1;35;45m P \033[0m");
    const_46 = new str("\033[1;32;42m G \033[0m");
    const_47 = new str("__main__");

    __name__ = new str("__main__");

    LEVEL_OF_PRECALCULATION = 4;
    R = const_41;
    W = const_42;
    B = const_43;
    Y = const_44;
    P = const_45;
    G = const_46;
    TOP = 0;
    FRONT = 1;
    BOTTOM = 2;
    BACK = 3;
    LEFT = 4;
    RIGHT = 5;
    preSolutionCubes = (new dict<str *, tuple2<list<list<list<str *> *> *> *, str *> *>());
    if (__eq(__name__, const_47)) {
        __ss_main();
    }
}

} // module namespace

int main(int __ss_argc, char **__ss_argv) {
    __shedskin__::__init();
    __sys__::__init(__ss_argc, __ss_argv);
    __math__::__init();
    __time__::__init();
    __random__::__init();
    __string__::__init();
    __datetime__::__init();
    __shedskin__::__start(__rubik__::__init);
}

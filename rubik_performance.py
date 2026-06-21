def copyCube(cube):
    #return [[row[:] for row in side] for side in cube]
    return [[[cube[0][0][0], cube[0][0][1]], [cube[0][1][0], cube[0][1][1]]],
            [[cube[1][0][0], cube[1][0][1]], [cube[1][1][0], cube[1][1][1]]],
            [[cube[2][0][0], cube[2][0][1]], [cube[2][1][0], cube[2][1][1]]],
            [[cube[3][0][0], cube[3][0][1]], [cube[3][1][0], cube[3][1][1]]],
            [[cube[4][0][0], cube[4][0][1]], [cube[4][1][0], cube[4][1][1]]],
            [[cube[5][0][0], cube[5][0][1]], [cube[5][1][0], cube[5][1][1]]]]
    
    
def get_key(cube):
    #return tuple([pos for side in cube for row in side for pos in row]).__hash__()
    li = (cube[0][0][0], cube[0][0][1], cube[0][1][0], cube[0][1][1],
            cube[1][0][0], cube[1][0][1], cube[1][1][0], cube[1][1][1],
            cube[2][0][0], cube[2][0][1], cube[2][1][0], cube[2][1][1],
            cube[3][0][0], cube[3][0][1], cube[3][1][0], cube[3][1][1],
            cube[4][0][0], cube[4][0][1], cube[4][1][0], cube[4][1][1],
            cube[5][0][0], cube[5][0][1], cube[5][1][0], cube[5][1][1])
    res = 0
    factor = 6
    for a in li:
        res += factor*a
        factor *= factor 
    
    return res
    #.__hash__()
            

TOP = 0
FRONT = 1
BOTTOM = 2
BACK = 3
LEFT = 4
RIGHT = 5
            
def leftDown(cube):
    (cube[TOP][0][0], cube[TOP][1][0],
    cube[FRONT][0][0], cube[FRONT][1][0],
    cube[BOTTOM][0][0], cube[BOTTOM][1][0],
    cube[BACK][0][0], cube[BACK][1][0],
    cube[LEFT][0][0], cube[LEFT][0][1],
    cube[LEFT][1][0], cube[LEFT][1][1]) = (cube[BACK][0][0], cube[BACK][1][0],
                                           cube[TOP][0][0], cube[TOP][1][0],
                                           cube[FRONT][0][0], cube[FRONT][1][0],
                                           cube[BOTTOM][0][0], cube[BOTTOM][1][0],
                                           cube[LEFT][1][0], cube[LEFT][0][0],
                                           cube[LEFT][1][1], cube[LEFT][0][1])
    return cube

def rightDown(cube):
    (cube[TOP][0][1], cube[TOP][1][1],
    cube[FRONT][0][1], cube[FRONT][1][1],
    cube[BOTTOM][0][1], cube[BOTTOM][1][1],
    cube[BACK][0][1], cube[BACK][1][1],
    cube[RIGHT][0][0], cube[RIGHT][0][1],
    cube[RIGHT][1][0], cube[RIGHT][1][1]) = (cube[BACK][0][1], cube[BACK][1][1],
                                           cube[TOP][0][1], cube[TOP][1][1],
                                           cube[FRONT][0][1], cube[FRONT][1][1],
                                           cube[BOTTOM][0][1], cube[BOTTOM][1][1],
                                           cube[RIGHT][0][1], cube[RIGHT][1][1],
                                           cube[RIGHT][0][0], cube[RIGHT][1][0])
    return cube

def rightUp(cube):
    rightDown(cube)
    rightDown(cube)
    rightDown(cube)
    return cube

def leftUp(cube):
    leftDown(cube)
    leftDown(cube)
    leftDown(cube)
    return cube

def topLeft(cube):
    (cube[FRONT][0][0], cube[FRONT][0][1],
    cube[LEFT][0][0], cube[LEFT][0][1],
    cube[BACK][1][0], cube[BACK][1][1],
    cube[RIGHT][0][0], cube[RIGHT][0][1],
    cube[TOP][0][0], cube[TOP][0][1],
    cube[TOP][1][0], cube[TOP][1][1]) = (cube[RIGHT][0][0], cube[RIGHT][0][1],
                                         cube[FRONT][0][0], cube[FRONT][0][1],
                                         cube[LEFT][0][1], cube[LEFT][0][0],
                                         cube[BACK][1][1], cube[BACK][1][0],
                                         cube[TOP][1][0], cube[TOP][0][0],
                                         cube[TOP][1][1], cube[TOP][0][1])
    return cube

def topRight(cube):
    topLeft(cube)
    topLeft(cube)
    topLeft(cube)
    return cube

def bottomLeft(cube):
    (cube[FRONT][1][0], cube[FRONT][1][1],
    cube[LEFT][1][0], cube[LEFT][1][1],
    cube[BACK][0][0], cube[BACK][0][1],
    cube[RIGHT][1][0], cube[RIGHT][1][1],
    cube[BOTTOM][0][0], cube[BOTTOM][0][1],
    cube[BOTTOM][1][0], cube[BOTTOM][1][1]) = (cube[RIGHT][1][0], cube[RIGHT][1][1],
                                         cube[FRONT][1][0], cube[FRONT][1][1],
                                         cube[LEFT][1][1], cube[LEFT][1][0],
                                         cube[BACK][0][1], cube[BACK][0][0],
                                         cube[BOTTOM][0][1], cube[BOTTOM][1][1],
                                         cube[BOTTOM][0][0], cube[BOTTOM][1][0])
    return cube

def bottomRight(cube):
    bottomLeft(cube)
    bottomLeft(cube)
    bottomLeft(cube)
    return cube

def frontClockwise(cube):
    (cube[TOP][1][0], cube[TOP][1][1],
    cube[LEFT][0][1], cube[LEFT][1][1],
    cube[BOTTOM][0][0], cube[BOTTOM][0][1],
    cube[RIGHT][0][0], cube[RIGHT][1][0],
    cube[FRONT][0][0], cube[FRONT][0][1],
    cube[FRONT][1][0], cube[FRONT][1][1]) = (cube[LEFT][1][1], cube[LEFT][0][1],
                                            cube[BOTTOM][0][0], cube[BOTTOM][0][1],
                                            cube[RIGHT][1][0], cube[RIGHT][0][0],
                                            cube[TOP][1][0], cube[TOP][1][1],
                                            cube[FRONT][1][0], cube[FRONT][0][0],
                                            cube[FRONT][1][1], cube[FRONT][0][1])
    return cube

def frontAntiClockwise(cube):
    frontClockwise(cube)
    frontClockwise(cube)
    frontClockwise(cube)
    return cube

def backClockwise(cube):
    (cube[TOP][0][0], cube[TOP][0][1],
    cube[LEFT][0][0], cube[LEFT][1][0],
    cube[BOTTOM][1][0], cube[BOTTOM][1][1],
    cube[RIGHT][0][1], cube[RIGHT][1][1],
    cube[BACK][0][0], cube[BACK][0][1],
    cube[BACK][1][0], cube[BACK][1][1]) = (cube[LEFT][1][0], cube[LEFT][0][0],
                                            cube[BOTTOM][1][0], cube[BOTTOM][1][1],
                                            cube[RIGHT][1][1], cube[RIGHT][0][1],
                                            cube[TOP][0][0], cube[TOP][0][1],
                                            cube[BACK][0][1], cube[BACK][1][1],
                                            cube[BACK][0][0], cube[BACK][1][0])
    return cube

def backAntiClockwise(cube):
    backClockwise(cube)
    backClockwise(cube)
    backClockwise(cube)
    return cube
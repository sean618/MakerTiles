
import os
import pcbnew
import KicadParser as KicadParser

board_dir = "../Boards"
output_file = '../Boards/Panel4/v0.8/Panel4.kicad_pcb'


board_table = [
    # Put all boards that stick out on the top row

    #27/34, 54/61

    # { 'path':"Battery/v0.5/Battery.kicad_pcb",                      'reference_prefix' : "BAT", 'length' : 63, 'x':None, 'row':0 },
    # { 'path':"ButtonExpansion/v0.5/ButtonExpansion.kicad_pcb",      'reference_prefix' : "BEX", 'length' : 63, 'x':None, 'row':0 },
    # { 'path':"Servo/v0.5/Servo.kicad_pcb",                          'reference_prefix' : "SER", 'length' : 63, 'x':None, 'row':1 },
    # { 'path':"Motor/v0.5/Motor.kicad_pcb",                          'reference_prefix' : "MOT", 'length' : 63, 'x':None, 'row':1 },
    # { 'path':"Screen/v0.5/Screen.kicad_pcb",                        'reference_prefix' : "SCR", 'length' : 63, 'x':None, 'row':2 },
    # { 'path':"LedMatrix/v0.5/LedMatrix.kicad_pcb",                  'reference_prefix' : "MAT", 'length' : 63, 'x':None, 'row':2 },


    # { 'path':"USB/v0.5/USB.kicad_pcb",                              'reference_prefix' : "USB", 'length' : 34, 'x':None, 'row':0 },
    # { 'path':"Wireless/v0.5/Wireless.kicad_pcb",                    'reference_prefix' : "WIR", 'length' : 34, 'x':None, 'row':0 },
    # { 'path':"Joystick/v0.5/Joystick.kicad_pcb",                    'reference_prefix' : "JOY", 'length' : 63, 'x':None, 'row':0 },

    # { 'path':"Distance/v0.5/Distance.kicad_pcb",            'reference_prefix' : "DIS", 'length' : 34, 'x':None, 'row':1 },
    # { 'path':"Button/v0.5/Button.kicad_pcb",                'reference_prefix' : "BUT", 'length' : 34, 'x':None, 'row':1 },
    # { 'path':"Buttons/v0.5/Buttons.kicad_pcb",              'reference_prefix' : "BTS", 'length' : 63, 'x':None, 'row':1 },

    # { 'path':"Temperature/v0.5/Temp.kicad_pcb",             'reference_prefix' : "TMP", 'length' : 34, 'x':None, 'row':2 },
    # { 'path':"LED/v0.5/LED.kicad_pcb",                      'reference_prefix' : "LED", 'length' : 34, 'x':None, 'row':2 },
    # { 'path':"Dial/v0.5/Dial.kicad_pcb",                    'reference_prefix' : "DIA", 'length' : 63, 'x':None, 'row':2 },

    # { 'path':"LightSound/v0.5/LightSound.kicad_pcb",        'reference_prefix' : "LIG", 'length' : 34, 'x':None, 'row':3 },
    # { 'path':"USBPower/v0.5/USBPower.kicad_pcb",             'reference_prefix' : "POW", 'length' : 34, 'x':None, 'row':3 },




    # ============== 16 in one ============== #


    { 'path':"Battery/v0.6/Battery.kicad_pcb",                      'reference_prefix' : "BAT", 'length' : 63, 'x':None, 'row':0 },
    { 'path':"Speaker/v0.8/Speaker.kicad_pcb",                      'reference_prefix' : "SPK", 'length' : 63, 'x':None, 'row':0 },
    # 2
    { 'path':"Motor/v0.7/Motor.kicad_pcb",                          'reference_prefix' : "MOT", 'length' : 63, 'x':None, 'row':1 },
    { 'path':"LightSound/v0.6/LightSound.kicad_pcb",                'reference_prefix' : "LIG", 'length' : 34, 'x':None, 'row':1 },


    # # 1
    # { 'path':"Battery/v0.5/Battery.kicad_pcb",                      'reference_prefix' : "BAT", 'length' : 63, 'x':None, 'row':0 },
    # { 'path':"ButtonExpansion/v0.5/ButtonExpansion.kicad_pcb",      'reference_prefix' : "BEX", 'length' : 63, 'x':None, 'row':0 },
    # { 'path':"USB/v0.5/USB.kicad_pcb",                              'reference_prefix' : "USB", 'length' : 34, 'x':None, 'row':0 },
    # { 'path':"Wireless/v0.5/Wireless.kicad_pcb",                    'reference_prefix' : "WIR", 'length' : 34, 'x':None, 'row':0 },

    # # 2
    # { 'path':"Servo/v0.5/Servo.kicad_pcb",          'reference_prefix' : "SER", 'length' : 63, 'x':None, 'row':1 },
    # { 'path':"Motor/v0.5/Motor.kicad_pcb",          'reference_prefix' : "MOT", 'length' : 63, 'x':None, 'row':1 },
    # { 'path':"Button/v0.5/Button.kicad_pcb",        'reference_prefix' : "BUT", 'length' : 34, 'x':None, 'row':1 },
    # { 'path':"Distance/v0.5/Distance.kicad_pcb",    'reference_prefix' : "DIS", 'length' : 34, 'x':None, 'row':1 },

    # # 3
    # { 'path':"Joystick/v0.5/Joystick.kicad_pcb",    'reference_prefix' : "JOY", 'length' : 63, 'x':None, 'row':2 },
    # { 'path':"Dial/v0.5/Dial.kicad_pcb",            'reference_prefix' : "DIA", 'length' : 63, 'x':None, 'row':2 },
    # { 'path':"Temperature/v0.5/Temp.kicad_pcb",            'reference_prefix' : "TMP", 'length' : 34, 'x':None, 'row':2 },
    # { 'path':"LED/v0.5/LED.kicad_pcb",              'reference_prefix' : "LED", 'length' : 34, 'x':None, 'row':2 },

    # # 4
    # { 'path':"Buttons/v0.5/Buttons.kicad_pcb",          'reference_prefix' : "BTS", 'length' : 63, 'x':None, 'row':3 },
    # { 'path':"Buttons/v0.5/Buttons.kicad_pcb",          'reference_prefix' : "BLK", 'length' : 63, 'x':None, 'row':3 },
    # { 'path':"LightSound/v0.5/LightSound.kicad_pcb",        'reference_prefix' : "LIG", 'length' : 34, 'x':None, 'row':3 },
    # { 'path':"USBPower/v0.5/USBPower.kicad_pcb",             'reference_prefix' : "POW", 'length' : 34, 'x':None, 'row':3 },
    # # { 'path':"Distance/v0.5/Distance.kicad_pcb",    'reference_prefix' : "WIR", 'length' : 34, 'x':None, 'row':3 },

    # # 5
    # { 'path':"Screen/v0.5/Screen.kicad_pcb",        'reference_prefix' : "SCR", 'length' : 63, 'x':None, 'row':4 },
    # { 'path':"LedMatrix/v0.5/LedMatrix.kicad_pcb",  'reference_prefix' : "MAT", 'length' : 63, 'x':None, 'row':4 },


    
]


# row_positions = [0, 35, 70]
row_positions = [0, 27, 54, 81, 108]
x_min = 0
x_max = 0
h_space_between_boards = 0.5

x = 0
row = 0
for board_info in board_table:
    board_info['y'] = row_positions[board_info['row']]
    if board_info['row'] != row:
        row += 1;
        x = 0
    if board_info['x'] == None:
        board_info['x'] = x
    x += board_info['length'] + h_space_between_boards
    x_max = max(x_max, x)

def moveEverything(board, x, y):
    scaling_factor = 1000000; # Kicad seems to use units of one millionth of a mm
    moveVector = pcbnew.VECTOR2I(int(x*scaling_factor), int(y*scaling_factor))
    for m in board.GetFootprints():
        # m.GetPosition()
        # pcbnew.wxPoint(
        # new_pos = m.GetPosition().__add__(moveVector)
        # m.SetPosition(new_pos)
        m.Move(moveVector)
    for t in board.GetTracks():
        t.Move(moveVector)
    for d in board.GetDrawings():
        d.Move(moveVector)
    for z in board.Zones():
        z.Move(moveVector)

def addPrefixToAllReferences(board, prefix):
    for m in board.GetFootprints():
        ref = m.GetReference()
        m.SetReference(prefix + ref)

def createTmpBoardsShiftedToCorrectLocation(board_table):
    tmpFilenames = []
    for board_info in board_table:
        prefix = board_info['reference_prefix']
        board = pcbnew.LoadBoard(board_dir + '/' + board_info['path']);
        moveEverything(board, board_info['x'], board_info['y'])
        addPrefixToAllReferences(board, prefix)
        tmpFilename = prefix + "_tmp.kicad_pcb"
        pcbnew.SaveBoard(tmpFilename, board)
        tmpFilenames.append(tmpFilename)
    return tmpFilenames

def stripOutBoardSettings(boardAst):
    # *** ASSUMPTION THAT MIGHT BREAK
    # Each board has board specific settings
    # To merge them we need only one lot of settings.
    # We assume all boards have the same settings and 
    # just use the first boards settings 
    nodesToRemove = []
    for majorNode in boardAst: 
        if (majorNode == "kicad_pcb") \
                or (majorNode[0] == 'version') \
                or (majorNode[0] == 'host') \
                or (majorNode[0] == 'general') \
                or (majorNode[0] == 'page') \
                or (majorNode[0] == 'layers') \
                or (majorNode[0] == 'setup') \
                or (majorNode[0] == 'net_class'):
            nodesToRemove.append(majorNode)
    for node in nodesToRemove:
        boardAst.remove(node)

def createNetRenamingMap(boardPrefix, boardAst, netIndex):
    # Each board has it's own indicies for its nets
    # We need to ensure every new board has its net indicies shifted so it doesn't clash
    # Also if we give them different names then kicad won't try and connect nets between boards
    netMap = {}
    for majorNode in boardAst:
        if (majorNode[0] == 'net'):
            prevIndex, prevName = majorNode[1], majorNode[2]
            if prevIndex == '0':
                netMap[prevIndex] = {'newIndex':0, 'newName':prevName}
            else:
                netMap[prevIndex] = {'newIndex':netIndex, 'newName': '"' + boardPrefix + "_" + prevName[1:]}
                # print(netMap[prevIndex]['newName'])
            netIndex += 1
    return (netMap, netIndex)

def recursivelyReplaceNetNames(boardAst, netMap):
    lastNetNodeName = None
    for node in boardAst:
        if type(node) == list:
            # If net replace the index with a new one
            if node[0] == 'net':
                prevIndex = node[1]
                newValues = netMap[prevIndex]
                node[1] = str(newValues['newIndex'])
                if len(node) > 2:
                    node[2] = newValues['newName']
                else:
                    lastNetNodeName = newValues['newName']
            elif node[0] == "net_name":
                node[1] = lastNetNodeName
                lastNetNodeName = None
            # Else keep recursing
            else:
                recursivelyReplaceNetNames(node, netMap)

def updateRecordedNumDrawings(boardAst, increment):
    for node in boardAst:
        if type(node) == list:
            if node[0] == 'general':
                for sub_node in node:
                    if sub_node[0] == 'drawings':
                        sub_node[1] = str(int(sub_node[1]) + increment)
                return


def addText(boardAst, text, pos, layer, fontSize, thickness):
    posNode = ['at', str(pos[0]), str(pos[1])]
    layerNode = ['layer', layer]
    sizeNode = ['size', str(fontSize[0]), str(fontSize[1])]
    thicknessNode = ['thickness', str(thickness)]
    fontNode = ['font', sizeNode, thicknessNode]
    effectsNode = ['effects', fontNode]
    boardAst.append(['gr_text', text, posNode, layerNode, effectsNode])
    updateRecordedNumDrawings(boardAst, 1)

def addLine(boardAst, start, end, layer, width):
    startNode = ['start', str(start[0]), str(start[1])]
    endNode = ['end', str(end[0]), str(end[1])]
    layerNode = ['layer', str(layer)]
    widthNode = ['width', str(width)]
    tstampNode = ['tstamp', '5E419963']
    boardAst.append(['gr_line', startNode, endNode, layerNode, widthNode, tstampNode])
    updateRecordedNumDrawings(boardAst, 1)

def addVcutAndText(boardAst, vcutIndex, start, end):
    addLine(boardAst, start, end, 'F.SilkS', 0.5)
    addText(boardAst, 'V_CUT' + str(vcutIndex), [start[0]-10, start[1]], 'F.SilkS', [3, 3], 0.6)


def addVcutsInFsilk(boardAst):
    x_start = x_min-20
    x_end = x_max+20
    vcutIndex = 1
    for y in row_positions:
        # Draw line at top of row - but not the very first row
        if y != min(row_positions):
            addVcutAndText(boardAst, vcutIndex, [x_start, y-0.25], [x_end, y-0.25])
            vcutIndex += 1
        # Draw line at bottom of row - but not the very last row
        if y != max(row_positions):
            addVcutAndText(boardAst, vcutIndex, [x_start, y+25.25], [x_end, y+25.25])
            vcutIndex += 1


def addToMainAst(mainAst, boardAst):
    for majorNode in boardAst:
        mainAst.append(majorNode)

def mergeBoardsIntoPanel(boardFilenames):
    index = 0
    netIndex = 0
    for filename in boardFilenames:
        boardPrefix = filename.split('_tmp')[0]
        boardFile = open(filename)
        boardAst = KicadParser.parse(boardFile.read())[0]
        boardFile.close()
        netMap, netIndex = createNetRenamingMap(boardPrefix, boardAst, netIndex)
        recursivelyReplaceNetNames(boardAst, netMap)
        if (index == 0):
            mainAst = boardAst
        else:
            stripOutBoardSettings(boardAst)
            addToMainAst(mainAst, boardAst)
        index += 1
    if os.path.isfile(output_file):
        old_file = output_file + '_old'
        if os.path.isfile(old_file):
            os.remove(old_file)
        os.rename(output_file, output_file + '_old')
    # addVcutsInFsilk(mainAst)
    panelFile = open(output_file, 'w')
    panelFile.write(KicadParser.unparse(mainAst))
    panelFile.close()


tmpFilenames = createTmpBoardsShiftedToCorrectLocation(board_table)
mergeBoardsIntoPanel(tmpFilenames)
for filename in tmpFilenames:
    os.remove(filename)



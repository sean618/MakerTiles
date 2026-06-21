
import KicadParser

file = "../Boards/LedMatrix/v0.5/LedMatrix.kicad_pcb"
contents = open(file).read()

# contents = '(username "Hello")'
# contents = '(username Hello)'

boardAst = KicadParser.parse(contents)[0]

spacing = 6.62
num_columns = 7
starting_x = 8.15-.7
starting_y = 8.15-.7

def index_to_pos(index):
    column = index % num_columns
    row = int(index / num_columns)
    new_y = starting_y + (row * spacing)
    new_x = starting_x + (column * spacing)
    return (new_x, new_y)


for item in boardAst:
    if item[0] == "footprint" and item[1] == '"boards:LED_WS2812B_PLCC4_5.0x5.0mm_P3.2mm"':  #'"boards:xl_ws2812b_small"':
        led = item
        # print(led)
        pos = led[4]
        x_pos = pos[1]
        y_pos = pos[2]
        reference = led[5]

        for p in item:
            if p[0] == "property" and p[1] == '"Reference"':
                name = p[2]
                break

        index = int(name[2:-1])
        new_x, new_y = index_to_pos(index-1 )
        print(name, x_pos, y_pos, "->", new_x, new_y)
        pos[1] = str(new_x)
        pos[2] = str(new_y)

with open(file, 'w') as f:
    print(KicadParser.unparse(boardAst), file=f)


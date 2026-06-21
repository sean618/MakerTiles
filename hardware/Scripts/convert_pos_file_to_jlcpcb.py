
import csv
import sys
import os 


# Adjusts positions and rotations to match JLCPCB format, plus add our own rotations

# We could just put the jlcpcb part number on the kicad symbol however it is easier to update and add 
# extra fields and extra rotations to this component_info_file

constant_rotation_offset = 270

field_mapping = {
    "Ref" : "Designator",
    "PosX" : "Mid X",
    "PosY" : "Mid Y",
    "Side" : "Layer",
    "Rot" : "Rotation",
}

def read_component_info(component_info_file):
    componentInfoTable = []
    with open(component_info_file) as csv_file:
        csv_reader = csv.DictReader(csv_file)
        for row in csv_reader:
            componentInfoTable.append(row)
    return componentInfoTable

def convert_pos_file(orig_file, new_file, component_info_file):
    componentInfoTable = read_component_info(component_info_file)
    components = {}
    with open(orig_file) as csv_file:
        csv_reader = csv.DictReader(csv_file)
        for row in csv_reader:
            ref = row['Ref']
            if "IGNORE" not in ref:
                component = {}
                component['Designator'] = ref
                component['Mid X'] = str(round(float(row['PosX'][:-2]), 4)) + "mm"
                component['Mid Y'] = str(round(float(row['PosY'][:-2]), 4)) + "mm"
                
                rotation = int(float(row['Rot']))
                value = row['Val']
                componentInfo = 0
                for info in componentInfoTable:
                    if info['Value'] == value:
                        if info['Footprint'] == row['Package'] or info['Footprint'] == "":
                            if componentInfo != 0:
                                raise Exception("Found multiple matching components for value: " + value)
                            componentInfo = info
                
                if componentInfo == 0:
                    print("Failed to finding matching componentsfor value: " + value)
                else:
                    if componentInfo['JLC pos rotation']:
                        rotation += int(componentInfo['JLC pos rotation'])

                if componentInfo['Type'] != "None":
                    if row['Side'] == "top":
                        rotation = (rotation + constant_rotation_offset) % 360
                        component['Layer'] = "Top"
                    elif row['Side'] == "bottom":
                        rotation = (rotation + constant_rotation_offset + 180) % 360
                        component['Layer'] = "Bottom"
                    else:
                        raise Exception('Unknown layer')
                    
                    component['Rotation'] = "{rot:.0f}".format(rot=rotation)
                    components[ref] = component

    # path, filename = os.path.split(orig)
    # file_basename, ext = os.path.splitext(filename)
    with open(new_file, 'w', newline='') as csv_file:
        writer = csv.DictWriter(csv_file, fieldnames=field_mapping.values(),extrasaction='ignore')
        writer.writeheader()
        for component in components.values():
            writer.writerow(component)
    

if __name__ == "__main__":
    convert_pos_file(sys.argv[1], sys.argv[2], sys.argv[3])


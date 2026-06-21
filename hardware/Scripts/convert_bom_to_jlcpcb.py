import os
import csv
import sys

# Partly just rearrange columns - however we also need to match a component to a JLCPCB part number

# We could just put the jlcpcb part number on the kicad symbol however it is easier to update and add 
# extra fields to this component_info_file, plus we use this file to record any footprint rotations as well

def find_matching_component(name, footprint, component_info, component_info_file):
    if footprint == "BoardText":
        return None
    if name not in component_info:
        print("Component " + name + " not found in " + component_info_file)
    if "LCSC_part_number" not in component_info[name] or component_info[name]["LCSC_part_number"] == "":
        if component_info[name]["Type"] != "None":
            print("Component " + name + " doesn't have a JLCPCB part no. in" + component_info_file)
    else:
        return component_info[name]
    return None

# Create a dictionary of components (based on our component info "Library")
def read_component_info_table(component_info_file):
    component_info = {}
    full_name_components = set([])
    with open(component_info_file) as csv_file:
        csv_reader = csv.DictReader(csv_file)
        for row in csv_reader:
            # For some components (like resistors and capacitors) we use
            # both the value and the footprint to distinguish the component
            name = row["Value"]
            full_name = row["Value"] + row["Footprint"]
            if name in component_info:
                full_name_components.add(name)
            component_info[name] = row
            component_info[full_name] = row
        # Remove the original value only entry for those that require value and footprint
        for name in full_name_components:
            del component_info[name]
    return (component_info, full_name_components)

def convert_bom(input_fn, output_fn, component_info_file):
    # Create a dictionary of components (based on our component info "Library")
    component_info, full_name_components = read_component_info_table(component_info_file)

    # Convert the original BOM
    new_components = []
    with open(input_fn) as input_csv:
        csv_reader = csv.DictReader(input_csv, delimiter=";")
        for row in csv_reader:
            name = row['Designation']

            # Check if this component requires a "full name"
            if name in full_name_components:
                name = row['Designation'] + row['Footprint']

            component = find_matching_component(name, row['Footprint'], component_info, component_info_file)

            if component != None:
                new_components.append({
                    "Comment" : row['Designation'],
                    "Designator" : row["Designator"],
                    "Footprint" : row["Footprint"],
                    "LCSC Part Number" : component["LCSC_part_number"]
                })
    
    # Write out the new BOM
    with open(output_fn, 'w', newline='') as csvfile:
        writer = csv.DictWriter(csvfile, fieldnames=new_components[0].keys())
        writer.writeheader()
        writer.writerows(new_components)

if __name__ == "__main__":
    convert_bom(sys.argv[1], sys.argv[2], sys.argv[3])
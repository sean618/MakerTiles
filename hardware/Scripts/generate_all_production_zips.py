import os
import sys
import zipfile
import csv


# /Applications/KiCad/kicad.app/Contents/Frameworks/Python.framework/Versions/Current/bin/python3 your_script.py
# sys.path.append(os.path.abspath("src"))
# sys.path.append("/Applications/KiCad/KiCad.app/Contents/Applications/pcbnew.app/Contents/MacOS")
# sys.path.append('/Applications/KiCad/kicad.app/Contents/SharedSupport/scripting/modules')

# !!!!!!!!!!!!!!!!!!!!!!!!!!!!!! #
# Run using: /Applications/KiCad/kicad.app/Contents/Frameworks/Python.framework/Versions/Current/bin/python3
# !!!!!!!!!!!!!!!!!!!!!!!!!!!!!! #


import pcbnew
import plot_gerbers
import convert_bom_to_jlcpcb as jlc_convert_bom
import convert_pos_file_to_jlcpcb as jlc_convert_pos
import convert_bom_to_pcbway as pcbway_convert_bom
import convert_pos_file_to_pcbway as pcbway_convert_pos



import wx

# Create a dummy wxApp instance if one doesn't already exist
if not wx.GetApp():
    app = wx.App(False)

base_directory = "../Boards"

# We used a component list. We could just put the jlcpcb part number on the kicad symbol
# however this list is easier to update and add extra fields to, plus we use this component
# list to record any footprint rotations as well
component_info_file = "../Libraries/BOM/ComponentInfo.csv"

boards = [
    # ("Panel4", "v0.8", "Panel4"),

    # ("Panel2", "v0.5", "Panel2"),
    # ("Panel3", "v0.5", "Panel3"),
    # ("Panel3", "v0.5", "Panel3B"),

    ("dial"    , "v0.10", "dial"),
    # ("Speaker"    , "v0.8", "Speaker"),
    # ("Battery"    , "v0.6", "Battery"),
    # ("Motor"      , "v0.7", "Motor"),
    # ("Screen"     , "v0.6", "Screen"),

    # ("Button"     , "v0.5", "Button"),
    # ("ButtonExpansion", "v0.5", "ButtonExpansion"),
    # ("Buttons"    , "v0.5", "Buttons"),
    # ("Dial"       , "v0.5", "Dial"),
    # ("Distance"   , "v0.5", "Distance"),
    # ( "IMU"      , "v0.5", "IMU"),
    # ("Joystick"   , "v0.5", "Joystick"),
    # ("LED"        , "v0.5", "LED"),
    # ("LedMatrix"  , "v0.6", "LedMatrix"),
    # ("LightSound" , "v0.5", "LightSound"),
    # ("Screen"     , "v0.5", "Screen"),
    # ("Servo"      , "v0.5", "Servo"),
    # ("Temperature", "v0.5", "Temp"),
    # ("USB"        , "v0.5", "USB"),
    # ("Wireless"   , "v0.5", "Wireless"),

    # # "NFC" : ("v0.1", "NFC"),
    # # "Magnet" : ("v0.1", "Magnet"),
    # # "FFCConverter" : ("v0.1", "FFCConverter"),
]
file_ext_names = {
    "-F_Silkscreen.gto" : True, # Required
    "-F_Paste.gtp" : True,
    "-F_Mask.gts" : True,
    "-F_Cu.gtl" : True,
    "-B_Silkscreen.gbo" : True,
    "-B_Paste.gbp" : True,
    "-B_Mask.gbs" : True,
    "-B_Cu.gbl" : True,
    "-Edge_Cuts.gm1" : True,
    "-PTH.drl" : True,
    "-NPTH.drl" : True,
    "-top-pos.csv" : True,
    "-bottom-pos.csv" : True,
    ".csv" : True,
    # "-In1_Cu.g2" : False, # Optional
    # "-In2_Cu.g2" : False, # Optional
}

def move_files_to_dir_if_not_already_there(current_dir, new_dir, board, files):
    for name, required in files.items():
        current_file = current_dir + board + name
        new_file = new_dir + board + name
        if os.path.exists(current_file):
            os.rename(current_file, new_file)
        elif required and not os.path.exists(new_file):
            print("Move: File does not exist:", current_file)

def compress_gerbers(dir, board, files):
    zipFile = dir + board + '.zip'
    if os.path.exists(zipFile):
        os.remove(zipFile)
    zip_archive = zipfile.ZipFile(zipFile, 'w')
    for name, required in files.items():
        file = dir + board + name
        if os.path.exists(file):
            zip_archive.write(file, compress_type = zipfile.ZIP_DEFLATED)
        elif required:
            print("File does not exist:", file)
    zip_archive.close()

def convert(board, new_dir, files, convert_bom_fn, convert_pos_fn, ext_name):
    files = file_ext_names.copy()

    # Convert the BOM to JLCPCB format
    convert_bom_fn(
        new_dir + board + ".csv",
        new_dir + board + "_bom_" + ext_name + ".csv",
        component_info_file)
    # Add the new converted file and remove the old
    files["_bom_" + ext_name + ".csv"] = True
    del files[".csv"]

    # Convert pos files to JLCPCB format
    for ext in ['-top-pos', '-bottom-pos']:
        convert_pos_fn(
            new_dir + board + ext + ".csv", 
            new_dir + board + ext + "_" + ext_name + ".csv",
            component_info_file)
        # Add the new converted file and remove the old
        files[ext  + "_" + ext_name + ".csv"] = True
        del files[ext + ".csv"]

    # Combine pos files for double sided placement
    top_file = new_dir + board + "-top-pos_" + ext_name + ".csv"
    bottom_file = new_dir + board + "-bottom-pos_" + ext_name + ".csv"
    combined_file = new_dir + board + "-combined-pos_" + ext_name + ".csv"
    with open(top_file, 'r') as top, open(bottom_file, 'r') as bottom, open(combined_file, 'w', newline='') as combined:
        reader1 = csv.reader(top)
        reader2 = csv.reader(bottom)
        writer = csv.writer(combined)
        for row in reader1:
            writer.writerow(row)

        skip = True
        for row in reader2:
            if skip:
                skip = False
            else:
                writer.writerow(row)

def generate_and_zip(format):
    for (board_dir, version, board) in boards:
        print("Starting " + board)
        directory = base_directory + "/" + board_dir + "/" + version + "/"
        output_folder = "Production/"
        new_dir = directory + output_folder
        files = file_ext_names

        # Move to a Production folder
        if not os.path.exists(new_dir):
            os.makedirs(new_dir)

        plot_gerbers.plot_production_files(board, directory, output_folder)

        # move_files_to_dir_if_not_already_there(directory, new_dir, board, files)

        if format == "jlcpcb":
            convert(board, new_dir, files, jlc_convert_bom.convert_bom, jlc_convert_pos.convert_pos_file, "jlcpcb")
        elif format == "pcbway":
            convert(board, new_dir, files, pcbway_convert_bom.convert_bom, pcbway_convert_pos.convert_pos_file, "pcbway")

        # Compress gerber files
        compress_gerbers(new_dir, board, files)



#generate_and_zip(False)
generate_and_zip("jlcpcb")


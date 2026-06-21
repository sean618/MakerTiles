import os
import pcbnew

def write_gerbers(board, output_folder):
    # Create a plot controller for generating Gerbers
    plot_controller = pcbnew.PLOT_CONTROLLER(board)

    # Set up Gerber plot options
    plot_options = plot_controller.GetPlotOptions()
    plot_options.SetOutputDirectory(output_folder)
    plot_options.SetPlotFrameRef(False)  # Disable plotting of the frame reference
    # plot_options.SetLineWidth(pcbnew.FromMM(0.1))  # Line width in mm
    plot_options.SetAutoScale(False)
    plot_options.SetUseAuxOrigin(True)  # Use auxiliary origin if set
    plot_options.SetMirror(False)
    plot_options.SetUseGerberAttributes(True)  # Enable Gerber attributes
    # plot_options.SetExcludeEdgeLayer(False)
    plot_options.SetPlotValue(True)
    plot_options.SetPlotReference(False)
    plot_options.SetUseGerberProtelExtensions(True)

    plot_plan = [
        ( 'F.Cu', pcbnew.F_Cu, 'Front Copper' ),
        ( 'B.Cu', pcbnew.B_Cu, 'Back Copper' ),
        ( 'F.Paste', pcbnew.F_Paste, 'Front Paste' ),
        ( 'B.Paste', pcbnew.B_Paste, 'Back Paste' ),
        ( 'F.Silkscreen', pcbnew.F_SilkS, 'Front SilkScreen' ),
        ( 'B.Silkscreen', pcbnew.B_SilkS, 'Back SilkScreen' ),
        ( 'F.Mask', pcbnew.F_Mask, 'Front Mask' ),
        ( 'B.Mask', pcbnew.B_Mask, 'Back Mask' ),
        ( 'Edge.Cuts', pcbnew.Edge_Cuts, 'Edges' ),
        ( 'Eco1.User', pcbnew.Eco1_User, 'Eco1 User' ),
        ( 'Eco2.User', pcbnew.Eco2_User, 'Eco1 User' ),
    ]
    
    for layer_info in plot_plan:
        plot_controller.SetLayer(layer_info[1])
        plot_controller.OpenPlotfile(layer_info[0], pcbnew.PLOT_FORMAT_GERBER, layer_info[2])
        plot_controller.PlotLayer()

    plot_controller.ClosePlot()


def write_drill_files(board, output_dir):
    drill_writer = pcbnew.EXCELLON_WRITER(board)
    drill_writer.SetFormat(False)  # Metric format
    drill_writer.CreateDrillandMapFilesSet(output_dir, True, False)


def write_pos_files(board, output_dir, board_name):
    mapping = {"top" : pcbnew.F_Cu, "bottom" : pcbnew.B_Cu}
    for side in "top", "bottom":
        with open(output_dir + board_name + "-" + side + "-pos.csv", 'w') as f:
            # Write the headers for the .pos file
            f.write("Ref,Val,Package,PosX,PosY,Rot,Side\n")

            # Iterate over all footprints on the board
            for footprint in board.GetFootprints():
                if mapping[side] == footprint.GetLayer():
                    ref = footprint.GetReference()
                    val = footprint.GetValue()
                    package = footprint.GetFPID().GetLibItemName()
                    pos = footprint.GetPosition()  # Get the footprint's position
                    posX = pcbnew.ToMM(pos.x)  # Convert position to millimeters
                    posY = pcbnew.ToMM(pos.y) * -1
                    rotation = footprint.GetOrientation().AsDegrees()
                    side = 'top' if footprint.GetLayer() == pcbnew.F_Cu else 'bottom'
                    f.write(f'"{ref}","{val}","{package}",{posX:.4f},{posY:.4f},{rotation:.1f},{side}\n')


def write_bom(board, output_dir, board_name):
    with open(output_dir + board_name + ".csv", 'w') as f:
        # Write the headers for the BOM file
        f.write('"Id";"Designator";"Footprint";"Quantity";"Designation";\n')
        # f.write("Ref, Value, Footprint, Quantity\n")
        
        # Dictionary to store component groups
        bom_dict = {}

        # Iterate over all footprints on the board
        for footprint in board.GetFootprints():
            ref = footprint.GetReference()
            val = footprint.GetValue()
            footprint_type = footprint.GetFPID().GetLibItemName().utf8_to_string()
            
            # Create a unique key for grouping components with the same value and footprint
            key = (val, footprint_type)
            
            # Add the component to the dictionary
            if key not in bom_dict:
                bom_dict[key] = []
            bom_dict[key].append(ref)

        # Write the grouped components to the BOM file
        id = 1
        for (value, footprint_type), refs in bom_dict.items():
            ref_list = ",".join(refs)
            quantity = len(refs)
            f.write(f'{id};"{ref_list}";"{footprint_type}";{quantity};"{value}";\n')
            id+=1

def plot_production_files(board_name, main_dir, output_folder):
    output_dir = main_dir + output_folder
    pcb_file_path = main_dir + board_name + ".kicad_pcb"
    # Load the PCB file
    board = pcbnew.LoadBoard(pcb_file_path)
    write_gerbers(board, output_folder)
    write_drill_files(board, output_dir)
    write_pos_files(board, output_dir, board_name)
    write_bom(board, output_dir, board_name)
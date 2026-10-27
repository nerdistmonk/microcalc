Microcalc is a visual calculator/spreadsheet made in the vein of VisiCalc.

# Features:

Loading/Saving in .mc and .csv formats (csv works with excel, though import from excel will only bring text/figures into
the program, you'll need to make new formulas due to fundamental differences between Microcalc and anything else)

Features several methods of entering formulas, including a new shorthand variant where for example you can sum up cells by
entering '=A0:A6', which would sum up cells A0 to A6. Or '=A0sA6' which will do the same thing, or '=SUM(A0:A6)', and so on.

Mouse support, left click to select a cell, left click again to enter data into it, right click any cell above the selected
cell to select (So if you are parked on A6, and you rightclick A0, you'll select A0 thru A5), you can then doubleclick the
empty cell below the selection to instantly sum the total. (it must be an empty cell, it'll flash red if it's not empty)

Uses sane keyboard bindings, with Ctrl + H providing an on screen help guide for controls + formula tips.

Autocompletion when navigating to a save or load location in the program by pressing tab, you can also load by invoking the
program directly as so: ./microcalc (filename.mc).

Color support in the program and the ability to color numbers in a cell, so you can use + to make a number green, or - to
make a number red for a visual aid.

Supports the four major groups of arithmetic, adding, subtracting, multiplication and division.

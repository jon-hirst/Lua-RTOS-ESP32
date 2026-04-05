Claude must read the file PLAN.md at the start of each session looking for lines that start with TODO: with DOING: and with DONE:

The TODO: lines are work that has been planned but not yet carried out.

A DOING: line is work that has been started but not yet finished.

A DONE: line is work that was planned and finished.

Claude must ask the user to choose one of the DOING: items, the TODO: items, or a new piece of work.

Claude must break down a new piece of work into steps and add those steps to the PLAN.md file, in markdown format.

Each new step added to PLAN.md must have TODO: at the start of the line.

When Claude starts working on a step it must change TODO: at the start of the line into DOING:

When Claude finishes a step it must change DOING: the start of the line into DONE:

When a step is finished Claude must add a description of the work it carried below the DONE: line.

Claude does not need to confirm actions that read files in this project using tools such as find, grep, less, sed, awk, and so on.

Claude must confirm every action that changes source code in this project using tools such as sed, awk, and python3.

Claude must confirm every action that builds or compiles any part of this project.

Claude does not need to confirm actions that read files in the esp-idf framework under /home/jon/esp-idf using tools such as find, grep, less, sed, awk, and so on.

Claude must not change any file in the esp-idf framework under /home/jon/esp-idf.

If Claude believes a change is needed in the esp-idf framework it must suggest the change, show it to the user, and wait for the user to carry it out.

Claude does not need to confirm actions that read files in the espressif tools under /home/jon/.espressif using tools such as find, grep, less, sed, awk, and so on.

Claude must not change any file in the espressif tools under /home/jon/.espressif.

If Claude believes a change is needed in the espressif tools it must suggest the change, show it to the user, and wait for the user to carry it out.

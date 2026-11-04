# src/core: SHARED by fermata and fermata:Live

Every file in this folder is built into **both** programs. Before changing anything here, read `COMPATIBILITY.md` at the top of the repository.
Nothing in this folder may include or call either program's own code (`src/ui`, `src/live`, `src/Main.cpp`). A change here must keep both
programs building and `tools/compat-check.sh` passing.

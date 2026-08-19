/*
 * lib_glue.c — deliberately empty for now.
 *
 * The top-level `carklight` shared library (see CMakeLists.txt) is
 * built by linking together carklight_core and, once they exist,
 * each backend's static library (docs/ADDENDUM.md §4) rather than
 * compiling any source directly at this level. CMake still needs at
 * least one translation unit to define the `carklight` target
 * against; this file is that placeholder. Real top-level glue (e.g.
 * PROPOSAL.md §3.4's ark_carklight_version()/ark_arklight_baseline())
 * lands in a later stage, in this same file, not a new one.
 */

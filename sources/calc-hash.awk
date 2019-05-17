{
    gsub(/[-]/, "_");
    last = split($2, path, "/");
    split(path[last], fn, ".");
    print "#define "toupper(fn[1])"_HASH \""$1"\"";
}

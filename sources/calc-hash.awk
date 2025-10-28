{
    name = $2;
    gsub(/[-\/]/, "_", name);
    gsub(/\.c$/, "", name);
    print "#define "toupper(name)"_HASH \""$1"\"";
}

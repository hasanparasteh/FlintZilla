# FlintZilla (FileZilla Clone) [![CodeFactor](https://www.codefactor.io/repository/github/hasanparasteh/flintzilla/badge)](https://www.codefactor.io/repository/github/hasanparasteh/flintzilla)
It's just a crappy 1:1 clone of filezilla. maybe all the features won't work but, you can connect to ftp server easily!

## How to compile?

In order to compile this code you need below requirements first:
1. wxWidgets -> 3.0.5
2. gnutls -> 3.7.x
3. libfilezilla -> 0.33.0
4. make
5. autoconf

After installing the requirements run below commands to compile and install it on your system!
```bash
> autoconf -i
> mkdir "compile" && cd compile
> ../configure --with-pugixml=builtin
> make
> sudo make install
```

## Statics
Language|files|blank|comment|code
:-------|-------:|-------:|-------:|-------:
C++|221|16975|2461|84581
C|111|8518|12583|47842
C/C++ Header|276|5576|5293|18646
Bourne Shell|1|2856|2420|14767
XML|20|6|14|2776
make|22|200|11|1830
m4|2|129|21|1135
Windows Resource File|4|26|51|113
Visual Studio Solution|1|0|1|54
Objective-C++|3|8|0|40
SVG|1|0|0|10
Windows Module Definition|1|1|0|6
--------|--------|--------|--------|--------
SUM:|663|34295|22855|171800

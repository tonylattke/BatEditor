#include "LanguageDefinitions.h"

#include <QCoreApplication>
#include <QFileInfo>

namespace {

QStringList words(const char *text)
{
    return QString::fromLatin1(text).split(QLatin1Char(' '), Qt::SkipEmptyParts);
}

// Comentarios /* */ y // de la familia C
void cStyleComments(LanguageDefinition &d)
{
    d.lineComment = QStringLiteral("//");
    d.blockCommentStart = QStringLiteral("/*");
    d.blockCommentEnd = QStringLiteral("*/");
}

QList<LanguageDefinition> buildLanguages()
{
    QList<LanguageDefinition> list;

    {
        LanguageDefinition d;
        d.name = QStringLiteral("C++");
        d.extensions = words("cc cpp cxx c++ h hh hpp hxx inl ipp ino tpp");
        d.keywords = words(
            "alignas alignof and asm auto break case catch class co_await co_return co_yield "
            "concept const consteval constexpr constinit const_cast continue decltype default "
            "delete do dynamic_cast else enum explicit export extern final for friend goto if "
            "inline mutable namespace new noexcept not operator or override private protected "
            "public register reinterpret_cast requires return sizeof static static_assert "
            "static_cast struct switch template this thread_local throw try typedef typeid "
            "typename union using virtual volatile while xor");
        d.types = words(
            "bool char char8_t char16_t char32_t double float int long short signed unsigned "
            "void wchar_t size_t ssize_t ptrdiff_t int8_t int16_t int32_t int64_t uint8_t "
            "uint16_t uint32_t uint64_t intptr_t uintptr_t std string string_view vector array "
            "map unordered_map set unordered_set deque list pair tuple function unique_ptr "
            "shared_ptr weak_ptr optional variant QString QStringList QList QVector QMap QHash "
            "QSet QObject QWidget QVariant QByteArray");
        d.constants = words("true false nullptr NULL");
        cStyleComments(d);
        d.preprocessor = true;
        list << d;
    }
    {
        LanguageDefinition d;
        d.name = QStringLiteral("C");
        d.extensions = words("c");
        d.keywords = words(
            "auto break case const continue default do else enum extern for goto if inline "
            "register restrict return sizeof static struct switch typedef union volatile while "
            "_Alignas _Alignof _Atomic _Generic _Noreturn _Static_assert _Thread_local");
        d.types = words(
            "bool char double float int long short signed unsigned void size_t ssize_t "
            "ptrdiff_t int8_t int16_t int32_t int64_t uint8_t uint16_t uint32_t uint64_t "
            "intptr_t uintptr_t FILE _Bool _Complex");
        d.constants = words("true false NULL EOF stdin stdout stderr");
        cStyleComments(d);
        d.preprocessor = true;
        list << d;
    }
    {
        LanguageDefinition d;
        d.name = QStringLiteral("Java");
        d.extensions = words("java");
        d.keywords = words(
            "abstract assert break case catch class const continue default do else enum exports "
            "extends final finally for goto if implements import instanceof interface module "
            "native new non-sealed open opens package permits private protected provides public "
            "record requires return sealed static strictfp super switch synchronized this throw "
            "throws transient try uses var volatile while with yield");
        d.types = words(
            "boolean byte char double float int long short void String Object Integer Long "
            "Double Float Boolean Character Byte Short List ArrayList Map HashMap Set HashSet "
            "Optional Stream Exception RuntimeException Throwable Thread Runnable Class Override");
        d.constants = words("true false null");
        cStyleComments(d);
        d.tripleQuotes = words("\"\"\"");   // text blocks
        d.annotations = true;
        list << d;
    }
    {
        LanguageDefinition d;
        d.name = QStringLiteral("Kotlin");
        d.extensions = words("kt kts");
        d.keywords = words(
            "abstract actual annotation as break by catch class companion const constructor "
            "continue crossinline data do else enum expect external final finally for fun get if "
            "import in infix init inline inner interface internal is lateinit noinline object "
            "open operator out override package private protected public reified return sealed "
            "set super suspend tailrec this throw try typealias val var vararg when where while");
        d.types = words(
            "Any Unit Nothing Int Long Short Byte Double Float Boolean Char String Array List "
            "MutableList Map MutableMap Set MutableSet Pair Triple");
        d.constants = words("true false null");
        cStyleComments(d);
        d.tripleQuotes = words("\"\"\"");
        d.variablePrefixes = QStringLiteral("$");   // plantillas "$nombre" / "${...}"
        d.annotations = true;
        list << d;
    }
    {
        LanguageDefinition d;
        d.name = QStringLiteral("C#");
        d.extensions = words("cs csx");
        d.keywords = words(
            "abstract as base break case catch checked class const continue default delegate do "
            "else enum event explicit extern finally fixed for foreach goto if implicit in "
            "interface internal is lock namespace new operator out override params private "
            "protected public readonly record ref required return sealed sizeof stackalloc static "
            "struct switch this throw try typeof unchecked unsafe using virtual volatile while "
            "async await var get set init yield when where with");
        d.types = words("bool byte char decimal double float int long object sbyte short string "
                        "uint ulong ushort void dynamic nint nuint List Dictionary Task");
        d.constants = words("true false null");
        cStyleComments(d);
        d.tripleQuotes = words("\"\"\"");   // raw strings
        d.preprocessor = true;
        list << d;
    }
    {
        LanguageDefinition d;
        d.name = QStringLiteral("Python");
        d.extensions = words("py pyw pyi pyx");
        d.fileNames = words("SConstruct SConscript");
        d.interpreters = words("python python2 python3");
        d.keywords = words(
            "and as assert async await break class continue def del elif else except finally "
            "for from global if import in is lambda nonlocal not or pass raise return try while "
            "with yield match case");
        d.types = words("int float complex str bytes bytearray bool list dict set frozenset tuple "
                        "object type self cls Exception");
        d.constants = words("True False None NotImplemented Ellipsis __name__ __file__");
        d.lineComment = QStringLiteral("#");
        d.tripleQuotes = words("\"\"\" '''");
        d.annotations = true;   // decoradores
        list << d;
    }
    {
        LanguageDefinition d;
        d.name = QStringLiteral("JavaScript");
        d.extensions = words("js mjs cjs jsx");
        d.interpreters = words("node nodejs");
        d.keywords = words(
            "async await break case catch class const continue debugger default delete do else "
            "export extends finally for from function get if import in instanceof let new of "
            "return set static super switch this throw try typeof var void while with yield");
        d.types = words("Array Object String Number Boolean Promise Map Set WeakMap WeakSet Date "
                        "RegExp Error JSON Math Symbol BigInt console window document");
        d.constants = words("true false null undefined NaN Infinity");
        cStyleComments(d);
        d.stringDelimiters = QStringLiteral("\"'`");
        d.annotations = true;
        list << d;
    }
    {
        LanguageDefinition d;
        d.name = QStringLiteral("TypeScript");
        d.extensions = words("ts tsx mts cts");
        d.interpreters = words("ts-node deno bun");
        d.keywords = words(
            "abstract as asserts async await break case catch class const continue debugger "
            "declare default delete do else enum export extends finally for from function get if "
            "implements import in infer instanceof interface is keyof let namespace new of "
            "override private protected public readonly return satisfies set static super switch "
            "this throw try type typeof unique var void while with yield");
        d.types = words("string number boolean bigint symbol any unknown never object void Array "
                        "Object Promise Map Set Record Partial Readonly Pick Omit Date Error");
        d.constants = words("true false null undefined NaN Infinity");
        cStyleComments(d);
        d.stringDelimiters = QStringLiteral("\"'`");
        d.annotations = true;
        list << d;
    }
    {
        LanguageDefinition d;
        d.name = QStringLiteral("Rust");
        d.extensions = words("rs");
        d.keywords = words(
            "as async await break const continue crate dyn else enum extern fn for if impl in let "
            "loop match mod move mut pub ref return self Self static struct super trait type "
            "unsafe use where while");
        d.types = words("i8 i16 i32 i64 i128 isize u8 u16 u32 u64 u128 usize f32 f64 bool char "
                        "str String Vec Option Result Box Rc Arc HashMap HashSet");
        d.constants = words("true false None Some Ok Err");
        cStyleComments(d);
        d.stringDelimiters = QStringLiteral("\"");
        list << d;
    }
    {
        LanguageDefinition d;
        d.name = QStringLiteral("Go");
        d.extensions = words("go");
        d.keywords = words(
            "break case chan const continue default defer else fallthrough for func go goto if "
            "import interface map package range return select struct switch type var");
        d.types = words("any bool byte complex64 complex128 error float32 float64 int int8 int16 "
                        "int32 int64 rune string uint uint8 uint16 uint32 uint64 uintptr");
        d.constants = words("true false nil iota");
        cStyleComments(d);
        d.stringDelimiters = QStringLiteral("\"'`");
        list << d;
    }
    {
        LanguageDefinition d;
        d.name = QStringLiteral("Swift");
        d.extensions = words("swift");
        d.keywords = words(
            "actor associatedtype async await break case catch class continue default defer deinit "
            "do else enum extension fallthrough fileprivate for func guard if import in init inout "
            "internal is let open operator private protocol public repeat rethrows return some "
            "static struct subscript super switch throw throws try typealias var where while");
        d.types = words("Int Int8 Int16 Int32 Int64 UInt Double Float Bool String Character Array "
                        "Dictionary Set Optional Any AnyObject Self Void");
        d.constants = words("true false nil self");
        cStyleComments(d);
        d.stringDelimiters = QStringLiteral("\"");
        d.tripleQuotes = words("\"\"\"");
        d.annotations = true;
        list << d;
    }
    {
        LanguageDefinition d;
        d.name = QStringLiteral("Groovy / Gradle");
        d.extensions = words("groovy gradle gvy");
        d.fileNames = words("Jenkinsfile");
        d.interpreters = words("groovy");
        d.keywords = words(
            "abstract as assert break case catch class continue def default do else enum extends "
            "final finally for if implements import in instanceof interface new package private "
            "protected public return static super switch this throw throws trait try var while");
        d.types = words("boolean byte char double float int long short void String Object List Map");
        d.constants = words("true false null");
        cStyleComments(d);
        d.tripleQuotes = words("\"\"\" '''");
        d.variablePrefixes = QStringLiteral("$");
        d.annotations = true;
        list << d;
    }
    {
        LanguageDefinition d;
        d.name = QStringLiteral("PHP");
        d.extensions = words("php phtml");
        d.interpreters = words("php");
        d.keywords = words(
            "abstract and as break case catch class clone const continue declare default do echo "
            "else elseif empty enddeclare endfor endforeach endif endswitch endwhile enum extends "
            "final finally fn for foreach function global goto if implements include include_once "
            "instanceof insteadof interface isset list match namespace new or print private "
            "protected public readonly require require_once return static switch throw trait try "
            "unset use var while xor yield");
        d.types = words("array bool callable float int iterable mixed object string void never self parent");
        d.constants = words("true false null TRUE FALSE NULL");
        cStyleComments(d);
        d.extraLineComments = words("#");
        d.variablePrefixes = QStringLiteral("$");
        list << d;
    }
    {
        LanguageDefinition d;
        d.name = QStringLiteral("Ruby");
        d.extensions = words("rb rake gemspec ru");
        d.fileNames = words("Rakefile Gemfile Vagrantfile Podfile Fastfile");
        d.interpreters = words("ruby");
        d.keywords = words(
            "alias and begin break case class def defined? do else elsif end ensure for if in "
            "module next not or redo rescue retry return self super then undef unless until when "
            "while yield require require_relative include extend attr_accessor attr_reader "
            "attr_writer private protected public");
        d.constants = words("true false nil");
        d.lineComment = QStringLiteral("#");
        d.blockCommentStart = QStringLiteral("=begin");
        d.blockCommentEnd = QStringLiteral("=end");
        d.stringDelimiters = QStringLiteral("\"'`");
        list << d;
    }
    {
        LanguageDefinition d;
        d.name = QStringLiteral("Lua");
        d.extensions = words("lua");
        d.interpreters = words("lua luajit");
        d.keywords = words(
            "and break do else elseif end for function goto if in local not or repeat return then "
            "until while");
        d.constants = words("true false nil self");
        d.lineComment = QStringLiteral("--");
        d.blockCommentStart = QStringLiteral("--[[");
        d.blockCommentEnd = QStringLiteral("]]");
        list << d;
    }
    {
        LanguageDefinition d;
        d.name = QStringLiteral("JSON");
        d.extensions = words("json jsonc json5 geojson webmanifest");
        d.fileNames = words(".babelrc .eslintrc .prettierrc");
        d.constants = words("true false null");
        d.lineComment = QStringLiteral("//");
        d.blockCommentStart = QStringLiteral("/*");
        d.blockCommentEnd = QStringLiteral("*/");
        d.stringDelimiters = QStringLiteral("\"");
        d.keyValue = true;
        d.highlightFunctions = false;
        list << d;
    }
    {
        LanguageDefinition d;
        d.name = QStringLiteral("YAML");
        d.extensions = words("yml yaml");
        d.fileNames = words(".clang-format .clang-tidy");
        d.constants = words("true false null yes no on off True False Null");
        d.lineComment = QStringLiteral("#");
        d.keyValue = true;
        d.highlightFunctions = false;
        list << d;
    }
    {
        LanguageDefinition d;
        d.name = QStringLiteral("TOML / INI");
        d.extensions = words("toml ini cfg conf properties desktop editorconfig");
        d.fileNames = words(".gitconfig .editorconfig .gitmodules Cargo.lock");
        d.constants = words("true false yes no on off");
        d.lineComment = QStringLiteral("#");
        d.extraLineComments = words(";");
        d.tripleQuotes = words("\"\"\" '''");
        d.keyValue = true;
        d.sectionHeaders = true;
        d.highlightFunctions = false;
        list << d;
    }
    {
        LanguageDefinition d;
        d.name = QStringLiteral("CMake");
        d.extensions = words("cmake");
        d.fileNames = words("CMakeLists.txt");
        d.keywords = words(
            "if elseif else endif foreach endforeach while endwhile function endfunction macro "
            "endmacro return break continue set unset option project cmake_minimum_required "
            "add_executable add_library target_link_libraries target_include_directories "
            "target_compile_definitions target_compile_options target_sources find_package "
            "include install message add_subdirectory set_target_properties list string file "
            "configure_file add_custom_command add_custom_target add_test enable_testing");
        d.constants = words("ON OFF TRUE FALSE PRIVATE PUBLIC INTERFACE REQUIRED COMPONENTS AND OR NOT");
        d.lineComment = QStringLiteral("#");
        d.stringDelimiters = QStringLiteral("\"");
        d.variablePrefixes = QStringLiteral("$");
        d.caseInsensitiveKeywords = true;
        d.highlightFunctions = false;
        list << d;
    }
    {
        LanguageDefinition d;
        d.name = QStringLiteral("Makefile");
        d.extensions = words("mk mak");
        d.fileNames = words("Makefile makefile GNUmakefile");
        d.interpreters = words("make");
        d.keywords = words("ifeq ifneq ifdef ifndef else endif include define endef export "
                           "unexport override vpath .PHONY");
        d.lineComment = QStringLiteral("#");
        d.variablePrefixes = QStringLiteral("$");
        d.keyValue = true;   // objetivos "all:" y variables "CC ="
        d.highlightFunctions = false;
        list << d;
    }
    {
        LanguageDefinition d;
        d.name = QStringLiteral("Dockerfile");
        d.extensions = words("dockerfile containerfile");
        d.fileNames = words("Dockerfile Containerfile");
        d.keywords = words("FROM AS RUN CMD LABEL MAINTAINER EXPOSE ENV ADD COPY ENTRYPOINT VOLUME "
                           "USER WORKDIR ARG ONBUILD STOPSIGNAL HEALTHCHECK SHELL");
        d.lineComment = QStringLiteral("#");
        d.variablePrefixes = QStringLiteral("$");
        d.highlightFunctions = false;
        list << d;
    }
    {
        LanguageDefinition d;
        d.name = QStringLiteral("Shell");
        d.extensions = words("sh bash zsh ksh fish");
        d.fileNames = words(".bashrc .bash_profile .zshrc .zprofile .profile .envrc");
        d.interpreters = words("sh bash zsh ksh dash fish");
        d.keywords = words("if then else elif fi case esac for while until do done in function "
                           "select return local export readonly declare source exit echo set unset "
                           "shift break continue trap eval exec cd");
        d.constants = words("true false");
        d.lineComment = QStringLiteral("#");
        d.variablePrefixes = QStringLiteral("$");
        d.highlightFunctions = false;
        list << d;
    }
    {
        LanguageDefinition d;
        d.name = QStringLiteral("PowerShell");
        d.extensions = words("ps1 psm1 psd1");
        d.interpreters = words("pwsh powershell");
        d.keywords = words(
            "begin break catch class continue data default do dynamicparam else elseif end enum "
            "exit filter finally for foreach from function if in param process return switch "
            "throw trap try until using while");
        d.constants = words("$true $false $null");
        d.lineComment = QStringLiteral("#");
        d.blockCommentStart = QStringLiteral("<#");
        d.blockCommentEnd = QStringLiteral("#>");
        d.variablePrefixes = QStringLiteral("$");
        d.caseInsensitiveKeywords = true;
        list << d;
    }
    {
        LanguageDefinition d;
        d.name = QStringLiteral("Batch");
        d.extensions = words("bat cmd");
        d.keywords = words("call cd chdir cls copy del dir echo else endlocal exist exit for goto "
                           "if in md mkdir move not off on pause popd pushd rd ren rmdir set "
                           "setlocal shift start title type do errorlevel defined");
        d.lineComment = QStringLiteral("::");
        d.extraLineComments = words("REM rem @REM @rem");
        d.stringDelimiters = QStringLiteral("\"");
        d.caseInsensitiveKeywords = true;
        d.highlightFunctions = false;
        list << d;
    }
    {
        LanguageDefinition d;
        d.name = QStringLiteral("SQL");
        d.extensions = words("sql");
        d.keywords = words(
            "select from where insert into values update set delete create table drop alter add "
            "index join left right inner outer full cross on group by order having limit offset "
            "union all distinct as and or not null is in like between primary key foreign "
            "references case when then else end exists view with returning default unique "
            "constraint begin commit rollback transaction grant revoke asc desc");
        d.types = words("int integer bigint smallint tinyint serial varchar char text date datetime "
                        "timestamp time boolean bool float real double decimal numeric json jsonb uuid blob");
        d.constants = words("true false");
        d.lineComment = QStringLiteral("--");
        d.blockCommentStart = QStringLiteral("/*");
        d.blockCommentEnd = QStringLiteral("*/");
        d.caseInsensitiveKeywords = true;
        list << d;
    }
    {
        LanguageDefinition d;
        d.name = QStringLiteral("CSS");
        d.extensions = words("css");
        d.blockCommentStart = QStringLiteral("/*");
        d.blockCommentEnd = QStringLiteral("*/");
        d.constants = words("inherit initial unset none auto important");
        d.annotations = true;   // @media, @import...
        d.keyValue = true;      // propiedades "color:"
        list << d;
    }
    {
        LanguageDefinition d;
        d.name = QStringLiteral("SCSS / Less");
        d.extensions = words("scss sass less");
        cStyleComments(d);
        d.keywords = words("if else each for while mixin include extend function return use forward");
        d.constants = words("inherit initial unset none auto important true false null");
        d.variablePrefixes = QStringLiteral("$");
        d.annotations = true;
        d.keyValue = true;
        list << d;
    }
    {
        LanguageDefinition d;
        d.name = QStringLiteral("HTML / XML");
        d.extensions = words("html htm xhtml xml svg xaml ui qrc xsd xsl xslt plist csproj vcxproj "
                             "props targets resx vue");
        d.blockCommentStart = QStringLiteral("<!--");
        d.blockCommentEnd = QStringLiteral("-->");
        d.highlightFunctions = false;
        list << d;
    }
    {
        LanguageDefinition d;
        d.name = QStringLiteral("Markdown");
        d.extensions = words("md markdown mdx");
        d.stringDelimiters = QStringLiteral("`");
        d.highlightFunctions = false;
        list << d;
    }
    return list;
}

// "#!/usr/bin/env -S python3 -u" -> "python3"; "#!/bin/bash" -> "bash"
QString shebangInterpreter(const QString &firstLine)
{
    if (!firstLine.startsWith(QLatin1String("#!")))
        return QString();
    const QStringList parts = firstLine.mid(2).split(QLatin1Char(' '), Qt::SkipEmptyParts);
    for (int i = 0; i < parts.size(); ++i) {
        const QString program = parts.at(i).section(QLatin1Char('/'), -1);
        if (program == QLatin1String("env") || program.startsWith(QLatin1Char('-')))
            continue;
        return program;
    }
    return QString();
}

} // namespace

namespace Languages {

const QList<LanguageDefinition> &all()
{
    static const QList<LanguageDefinition> list = buildLanguages();
    return list;
}

const LanguageDefinition &plainText()
{
    static const LanguageDefinition d = [] {
        LanguageDefinition p;
        p.name = QCoreApplication::translate("Languages", "Texto plano");
        p.stringDelimiters.clear();
        p.highlightFunctions = false;
        return p;
    }();
    return d;
}

const LanguageDefinition &byName(const QString &name)
{
    for (const LanguageDefinition &d : all())
        if (d.name == name)
            return d;
    return plainText();
}

const LanguageDefinition &forFile(const QString &filePath, const QString &firstLine)
{
    const QFileInfo info(filePath);
    const QString fileName = info.fileName();
    const QString ext = info.suffix().toLower();
    for (const LanguageDefinition &d : all()) {
        if (d.fileNames.contains(fileName))
            return d;
    }
    if (!ext.isEmpty()) {
        for (const LanguageDefinition &d : all()) {
            if (d.extensions.contains(ext))
                return d;
        }
    }
    // "Dockerfile.dev", "Makefile.win"...
    for (const LanguageDefinition &d : all()) {
        for (const QString &name : d.fileNames)
            if (fileName.startsWith(name + QLatin1Char('.')))
                return d;
    }
    const QString interpreter = shebangInterpreter(firstLine);
    if (!interpreter.isEmpty()) {
        for (const LanguageDefinition &d : all()) {
            for (const QString &program : d.interpreters)
                if (interpreter == program
                    || (interpreter.startsWith(program) && interpreter.mid(program.size()).front().isDigit()))
                    return d;
        }
    }
    return plainText();
}

} // namespace Languages

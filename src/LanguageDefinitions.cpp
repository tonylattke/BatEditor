#include "LanguageDefinitions.h"

#include <QFileInfo>

namespace {

QStringList words(const char *text)
{
    return QString::fromLatin1(text).split(QLatin1Char(' '), Qt::SkipEmptyParts);
}

QList<LanguageDefinition> buildLanguages()
{
    QList<LanguageDefinition> list;

    {
        LanguageDefinition d;
        d.name = QStringLiteral("C++");
        d.extensions = words("c cc cpp cxx c++ h hh hpp hxx inl ipp ino");
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
            "uint16_t uint32_t uint64_t intptr_t uintptr_t std string vector map unordered_map "
            "set unique_ptr shared_ptr weak_ptr optional variant QString QStringList QList "
            "QVector QMap QHash QObject QWidget");
        d.constants = words("true false nullptr NULL");
        d.lineComment = QStringLiteral("//");
        d.blockCommentStart = QStringLiteral("/*");
        d.blockCommentEnd = QStringLiteral("*/");
        d.preprocessor = true;
        list << d;
    }
    {
        LanguageDefinition d;
        d.name = QStringLiteral("C#");
        d.extensions = words("cs");
        d.keywords = words(
            "abstract as base break case catch checked class const continue default delegate do "
            "else enum event explicit extern finally fixed for foreach goto if implicit in "
            "interface internal is lock namespace new operator out override params private "
            "protected public readonly record ref return sealed sizeof stackalloc static struct "
            "switch this throw try typeof unchecked unsafe using virtual volatile while async "
            "await var get set init yield");
        d.types = words("bool byte char decimal double float int long object sbyte short string "
                        "uint ulong ushort void dynamic");
        d.constants = words("true false null");
        d.lineComment = QStringLiteral("//");
        d.blockCommentStart = QStringLiteral("/*");
        d.blockCommentEnd = QStringLiteral("*/");
        d.preprocessor = true;
        list << d;
    }
    {
        LanguageDefinition d;
        d.name = QStringLiteral("Python");
        d.extensions = words("py pyw pyi");
        d.keywords = words(
            "and as assert async await break class continue def del elif else except finally "
            "for from global if import in is lambda nonlocal not or pass raise return try while "
            "with yield match case");
        d.types = words("int float str bytes bool list dict set tuple object type self cls");
        d.constants = words("True False None");
        d.lineComment = QStringLiteral("#");
        d.tripleQuote = QStringLiteral("\"\"\"");
        list << d;
    }
    {
        LanguageDefinition d;
        d.name = QStringLiteral("JavaScript");
        d.extensions = words("js mjs cjs jsx ts tsx");
        d.keywords = words(
            "async await break case catch class const continue debugger default delete do else "
            "export extends finally for from function if import in instanceof let new of return "
            "static super switch this throw try typeof var void while with yield interface type "
            "implements enum public private protected readonly as");
        d.types = words("string number boolean any unknown never object Array Promise Map Set");
        d.constants = words("true false null undefined NaN Infinity");
        d.lineComment = QStringLiteral("//");
        d.blockCommentStart = QStringLiteral("/*");
        d.blockCommentEnd = QStringLiteral("*/");
        d.stringDelimiters = QStringLiteral("\"'`");
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
                        "str String Vec Option Result Box");
        d.constants = words("true false None Some Ok Err");
        d.lineComment = QStringLiteral("//");
        d.blockCommentStart = QStringLiteral("/*");
        d.blockCommentEnd = QStringLiteral("*/");
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
        d.types = words("bool byte complex64 complex128 error float32 float64 int int8 int16 "
                        "int32 int64 rune string uint uint8 uint16 uint32 uint64 uintptr");
        d.constants = words("true false nil iota");
        d.lineComment = QStringLiteral("//");
        d.blockCommentStart = QStringLiteral("/*");
        d.blockCommentEnd = QStringLiteral("*/");
        d.stringDelimiters = QStringLiteral("\"'`");
        list << d;
    }
    {
        LanguageDefinition d;
        d.name = QStringLiteral("JSON");
        d.extensions = words("json jsonc");
        d.constants = words("true false null");
        d.lineComment = QStringLiteral("//");
        d.stringDelimiters = QStringLiteral("\"");
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
            "target_compile_definitions target_compile_options find_package include install "
            "message add_subdirectory set_target_properties list string");
        d.constants = words("ON OFF TRUE FALSE PRIVATE PUBLIC INTERFACE REQUIRED COMPONENTS");
        d.lineComment = QStringLiteral("#");
        d.stringDelimiters = QStringLiteral("\"");
        d.caseInsensitiveKeywords = true;
        d.highlightFunctions = false;
        list << d;
    }
    {
        LanguageDefinition d;
        d.name = QStringLiteral("Shell");
        d.extensions = words("sh bash zsh");
        d.fileNames = words(".bashrc .zshrc .profile");
        d.keywords = words("if then else elif fi case esac for while until do done in function "
                           "return local export source exit echo");
        d.lineComment = QStringLiteral("#");
        list << d;
    }
    {
        LanguageDefinition d;
        d.name = QStringLiteral("YAML / TOML / INI");
        d.extensions = words("yml yaml toml ini cfg conf");
        d.constants = words("true false null yes no on off");
        d.lineComment = QStringLiteral("#");
        d.highlightFunctions = false;
        list << d;
    }
    {
        LanguageDefinition d;
        d.name = QStringLiteral("SQL");
        d.extensions = words("sql");
        d.keywords = words(
            "select from where insert into values update set delete create table drop alter add "
            "index join left right inner outer on group by order having limit offset union all "
            "distinct as and or not null is in like between primary key foreign references");
        d.types = words("int integer bigint smallint varchar char text date datetime timestamp "
                        "boolean float double decimal");
        d.lineComment = QStringLiteral("--");
        d.blockCommentStart = QStringLiteral("/*");
        d.blockCommentEnd = QStringLiteral("*/");
        d.caseInsensitiveKeywords = true;
        list << d;
    }
    {
        LanguageDefinition d;
        d.name = QStringLiteral("HTML / XML");
        d.extensions = words("html htm xml svg xaml ui qrc");
        d.blockCommentStart = QStringLiteral("<!--");
        d.blockCommentEnd = QStringLiteral("-->");
        d.highlightFunctions = false;
        list << d;
    }
    {
        LanguageDefinition d;
        d.name = QStringLiteral("Markdown");
        d.extensions = words("md markdown");
        d.stringDelimiters = QStringLiteral("`");
        d.highlightFunctions = false;
        list << d;
    }
    return list;
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
        p.name = QStringLiteral("Texto plano");
        p.stringDelimiters.clear();
        p.highlightFunctions = false;
        return p;
    }();
    return d;
}

const LanguageDefinition &forFile(const QString &filePath)
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
    return plainText();
}

} // namespace Languages

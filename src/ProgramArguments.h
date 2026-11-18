#pragma once
#include <QStringList>

struct ProgramArguments {
    QStringList values;
    QString error;
};

// Split argument text without evaluating variables, globbing, or shell commands.
inline ProgramArguments parseProgramArguments(const QString &text) {
    ProgramArguments result;
    QString value;
    QChar quote;
    bool escaped = false, started = false;
    for (const QChar c : text) {
        if (c.isNull() || c == '\n' || c == '\r') {
            result.error = "Program arguments must be a single line without NUL characters.";
            return result;
        }
        if (escaped) {
            value += c;
            escaped = false;
        } else if (c == '\\' && quote != '\'') {
            escaped = true;
            started = true;
        } else if (!quote.isNull()) {
            if (c == quote)
                quote = QChar();
            else
                value += c;
        } else if (c == '\'' || c == '"') {
            quote = c;
            started = true;
        } else if (c.isSpace()) {
            if (started) {
                result.values << value;
                value.clear();
                started = false;
            }
        } else {
            value += c;
            started = true;
        }
    }
    if (escaped)
        result.error = "Finish the escaped character after the trailing backslash.";
    else if (!quote.isNull())
        result.error = "Close the quoted program argument.";
    else if (started)
        result.values << value;
    return result;
}

// -exec-arguments accepts a raw shell argument string (not MI C-string tokens).
// Quote every value, including empty arguments and literal shell metacharacters.
inline QString quoteProgramArguments(const QStringList &arguments) {
    QStringList quoted;
    for (QString argument : arguments) {
        argument.replace("'", "'\\''");
        quoted << "'" + argument + "'";
    }
    return quoted.join(' ');
}

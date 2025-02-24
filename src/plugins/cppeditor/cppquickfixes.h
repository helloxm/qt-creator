// Copyright (C) 2016 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0

#pragma once

#include "cppeditor_global.h"
#include "cppquickfix.h"

///
/// Adding New Quick Fixes
///
/// When adding new Quick Fixes, make sure that the match() function is "cheap".
/// Otherwise, since the match() functions are also called to generate context menu
/// entries, the user might experience a delay opening the context menu.
///

namespace CppEditor {
namespace Internal {

void createCppQuickFixes();
void destroyCppQuickFixes();

class ExtraRefactoringOperations : public CppQuickFixFactory
{
public:
    void match(const CppQuickFixInterface &interface, QuickFixOperations &result) override;
};

/*!
  Adds an include for an undefined identifier or only forward declared identifier.

  Activates on: the undefined identifier
*/
class AddIncludeForUndefinedIdentifier : public CppQuickFixFactory
{
public:
    void match(const CppQuickFixInterface &interface, QuickFixOperations &result) override;
};

// Exposed for tests
class AddIncludeForUndefinedIdentifierOp: public CppQuickFixOperation
{
public:
    AddIncludeForUndefinedIdentifierOp(const CppQuickFixInterface &interface, int priority,
                                       const QString &include);
    void perform() override;

    QString include() const { return m_include; }

private:
    QString m_include;
};

class AddForwardDeclForUndefinedIdentifierOp: public CppQuickFixOperation
{
public:
    AddForwardDeclForUndefinedIdentifierOp(const CppQuickFixInterface &interface, int priority,
                                           const QString &fqClassName, int symbolPos);
private:
    void perform() override;

    const QString m_className;
    const int m_symbolPos;
};

/*!
  Rewrite
    a op b

  As
    b flipop a

  Activates on: <= < > >= == != && ||
*/
class FlipLogicalOperands: public CppQuickFixFactory
{
public:
    void match(const CppQuickFixInterface &interface, QuickFixOperations &result) override;
};

/*!
  Rewrite
    a op b -> !(a invop b)
    (a op b) -> !(a invop b)
    !(a op b) -> (a invob b)

  Activates on: <= < > >= == !=
*/
class InverseLogicalComparison: public CppQuickFixFactory
{
public:
    void match(const CppQuickFixInterface &interface, QuickFixOperations &result) override;
};

/*!
  Rewrite
    !a && !b

  As
    !(a || b)

  Activates on: &&
*/
class RewriteLogicalAnd: public CppQuickFixFactory
{
public:
    void match(const CppQuickFixInterface &interface, QuickFixOperations &result) override;
};

/*!
  Replace
     "abcd"
     QLatin1String("abcd")
     QLatin1Literal("abcd")

  With
     @"abcd"

  Activates on: the string literal, if the file type is a Objective-C(++) file.
*/
class ConvertCStringToNSString: public CppQuickFixFactory
{
public:
    void match(const CppQuickFixInterface &interface, QuickFixOperations &result) override;
};

/*!
  Base class for converting numeric literals between decimal, octal and hex.
  Does the base check for the specific ones and parses the number.

  Test cases:
    0xFA0Bu;
    0X856A;
    298.3;
    199;
    074;
    199L;
    074L;
    -199;
    -017;
    0783; // invalid octal
    0; // border case, allow only hex<->decimal

  Activates on: numeric literals
*/
class ConvertNumericLiteral: public CppQuickFixFactory
{
public:
    void match(const CppQuickFixInterface &interface, QuickFixOperations &result) override;
};

/*!
  Replace
    "abcd"

  With
    tr("abcd") or
    QCoreApplication::translate("CONTEXT", "abcd") or
    QT_TRANSLATE_NOOP("GLOBAL", "abcd")

  depending on what is available.

  Activates on: the string literal
*/
class TranslateStringLiteral: public CppQuickFixFactory
{
public:
    void match(const CppQuickFixInterface &interface, QuickFixOperations &result) override;
};

/*!
  Replace
    "abcd"  -> QLatin1String("abcd")
    @"abcd" -> QLatin1String("abcd") (Objective C)
    'a'     -> QLatin1Char('a')
    'a'     -> "a"
    "a"     -> 'a' or QLatin1Char('a') (Single character string constants)
    "\n"    -> '\n', QLatin1Char('\n')

  Except if they are already enclosed in
    QLatin1Char, QT_TRANSLATE_NOOP, tr,
    trUtf8, QLatin1Literal, QLatin1String

  Activates on: the string or character literal
*/

class WrapStringLiteral: public CppQuickFixFactory
{
public:
    void match(const CppQuickFixInterface &interface, QuickFixOperations &result) override;
};

/*!
  Turns "an_example_symbol" into "anExampleSymbol" and
  "AN_EXAMPLE_SYMBOL" into "AnExampleSymbol".

  Activates on: identifiers
*/
class ConvertToCamelCase : public CppQuickFixFactory
{
public:
    ConvertToCamelCase(bool test = false) : CppQuickFixFactory(), m_test(test) {}

    void match(const CppQuickFixInterface &interface, QuickFixOperations &result) override;

private:
    const bool m_test;
};

/*!
  Replace
    if (Type name = foo()) {...}

  With
    Type name = foo();
    if (name) {...}

  Activates on: the name of the introduced variable
*/
class MoveDeclarationOutOfIf: public CppQuickFixFactory
{
public:
    void match(const CppQuickFixInterface &interface, QuickFixOperations &result) override;
};

/*!
  Replace
    while (Type name = foo()) {...}

  With
    Type name;
    while ((name = foo()) != 0) {...}

  Activates on: the name of the introduced variable
*/
class MoveDeclarationOutOfWhile: public CppQuickFixFactory
{
public:
    void match(const CppQuickFixInterface &interface, QuickFixOperations &result) override;
};

/*!
  Replace
     if (something && something_else) {
     }

  with
     if (something)
        if (something_else) {
        }
     }

  and
    if (something || something_else)
      x;

  with
    if (something)
      x;
    else if (something_else)
      x;

    Activates on: && or ||
*/
class SplitIfStatement: public CppQuickFixFactory
{
public:
    void match(const CppQuickFixInterface &interface, QuickFixOperations &result) override;
};

/*!
  Rewrite
    int *a, b;

  As
    int *a;
    int b;

  Activates on: the type or the variable names.
*/
class SplitSimpleDeclaration: public CppQuickFixFactory
{
public:
    void match(const CppQuickFixInterface &interface, QuickFixOperations &result) override;
};

/*!
  Add curly braces to a if statement that doesn't already contain a
  compound statement. I.e.

  if (a)
      b;
  becomes
  if (a) {
      b;
  }

  Activates on: the if
*/
class AddBracesToIf: public CppQuickFixFactory
{
public:
    void match(const CppQuickFixInterface &interface, QuickFixOperations &result) override;
};

/*!
  Switches places of the parameter declaration under cursor
  with the next or the previous one in the parameter declaration list

  Activates on: parameter declarations
*/
class RearrangeParamDeclarationList : public CppQuickFixFactory
{
public:
    void match(const CppQuickFixInterface &interface, QuickFixOperations &result) override;
};

/*!
  Reformats a pointer, reference or rvalue reference type/declaration.

  Works also with selections (except when the cursor is not on any AST).

  Activates on: simple declarations, parameters and return types of function
                declarations and definitions, control flow statements (if,
                while, for, foreach) with declarations.
*/
class ReformatPointerDeclaration : public CppQuickFixFactory
{
public:
    void match(const CppQuickFixInterface &interface, QuickFixOperations &result) override;
};

/*!
  Adds missing case statements for "switch (enumVariable)"
 */
class CompleteSwitchCaseStatement: public CppQuickFixFactory
{
public:
    void match(const CppQuickFixInterface &interface, QuickFixOperations &result) override;
};

/*!
  Adds a declarations to a definition
 */
class InsertDeclFromDef: public CppQuickFixFactory
{
public:
    void match(const CppQuickFixInterface &interface, TextEditor::QuickFixOperations &result) override;
};

/*!
  Adds a definition for a declaration.
 */
class InsertDefFromDecl: public CppQuickFixFactory
{
public:
    void match(const CppQuickFixInterface &interface, TextEditor::QuickFixOperations &result) override;
};

class AddDeclarationForUndeclaredIdentifier : public CppQuickFixFactory
{
public:
    void match(const CppQuickFixInterface &interface,
               TextEditor::QuickFixOperations &result) override;

#ifdef WITH_TESTS
    void setMembersOnly() { m_membersOnly = true; }
#endif

private:
    void collectOperations(const CppQuickFixInterface &interface,
                           TextEditor::QuickFixOperations &result);

    // Returns whether to still do other checks.
    bool checkForMemberInitializer(const CppQuickFixInterface &interface,
                                   TextEditor::QuickFixOperations &result);

    void maybeAddMember(const CppQuickFixInterface &interface, CPlusPlus::Scope *scope,
                        const QByteArray &classTypeExpr, const CPlusPlus::ExpressionAST *initExpr,
                        TextEditor::QuickFixOperations &result);

    bool m_membersOnly = false;
};

/*!
  Adds a definition for any number of member function declarations.
 */
class InsertDefsFromDecls : public CppQuickFixFactory
{
public:
    void match(const CppQuickFixInterface &interface,
               TextEditor::QuickFixOperations &result) override;

    enum class Mode {
            Off,         // Testing: simulates user canceling the dialog
            Alternating, // Testing: simulates user choosing a different DefPos for every function
            User         // Normal interactive mode
    };
    void setMode(Mode mode) { m_mode = mode; }

private:
    Mode m_mode = Mode::User;
};

/*!
  Extracts the selected code and puts it to a function
 */
class ExtractFunction : public CppQuickFixFactory
{
public:
    using FunctionNameGetter = std::function<QString()>;

    ExtractFunction(FunctionNameGetter functionNameGetter = FunctionNameGetter());
    void match(const CppQuickFixInterface &interface, TextEditor::QuickFixOperations &result) override;

private:
    FunctionNameGetter m_functionNameGetter; // For tests to avoid GUI pop-up.
};

/*!
  Extracts the selected constant and converts it to a parameter of the current function.

  Activates on numeric, bool, character, or string literal in the function body.
 */
class ExtractLiteralAsParameter : public CppQuickFixFactory
{
public:
    void match(const CppQuickFixInterface &interface, TextEditor::QuickFixOperations &result) override;
};

/*!
  Converts the selected variable to a pointer if it is a stack variable or reference, or vice versa.

  Activates on variable declarations.
 */
class ConvertFromAndToPointer : public CppQuickFixFactory
{
public:
    void match(const CppQuickFixInterface &interface, TextEditor::QuickFixOperations &result) override;
};

/*!
  Adds getter and setter functions for a member variable
 */
class GenerateGetterSetter : public CppQuickFixFactory
{
public:
    void match(const CppQuickFixInterface &interface, TextEditor::QuickFixOperations &result) override;
};

/*!
  Adds getter and setter functions for several member variables
 */
class GenerateGettersSettersForClass : public CppQuickFixFactory
{
protected:
    void setTest() { m_test = true; }

private:
    void match(const CppQuickFixInterface &interface,
               TextEditor::QuickFixOperations &result) override;

    bool m_test = false;
};

/*!
  Adds missing members for a Q_PROPERTY
 */
class InsertQtPropertyMembers : public CppQuickFixFactory
{
public:
    void match(const CppQuickFixInterface &interface, TextEditor::QuickFixOperations &result) override;
};

/*!
  Converts a Qt 4 QObject::connect() to Qt 5 style.
 */
class ConvertQt4Connect : public CppQuickFixFactory
{
public:
    void match(const CppQuickFixInterface &interface, TextEditor::QuickFixOperations &result) override;
};

/*!
 Applies function signature changes
 */
class ApplyDeclDefLinkChanges: public CppQuickFixFactory
{
public:
    void match(const CppQuickFixInterface &interface, TextEditor::QuickFixOperations &result) override;
};

/*!
 Moves the definition of a member function outside the class or moves the definition of a member
 function or a normal function to the implementation file.
 */
class MoveFuncDefOutside: public CppQuickFixFactory
{
public:
    void match(const CppQuickFixInterface &interface, TextEditor::QuickFixOperations &result) override;
};

/*!
 Moves all member function definitions outside the class or to the implementation file.
 */
class MoveAllFuncDefOutside: public CppQuickFixFactory
{
public:
    void match(const CppQuickFixInterface &interface, TextEditor::QuickFixOperations &result) override;
};

/*!
 Moves the definition of a function to its declaration.
 */
class MoveFuncDefToDecl: public CppQuickFixFactory
{
public:
    void match(const CppQuickFixInterface &interface, TextEditor::QuickFixOperations &result) override;
};

/*!
  Assigns the return value of a function call or a new expression to a local variable
 */
class AssignToLocalVariable : public CppQuickFixFactory
{
public:
    void match(const CppQuickFixInterface &interface, TextEditor::QuickFixOperations &result) override;
};

/*!
  Optimizes a for loop to avoid permanent condition check and forces to use preincrement
  or predecrement operators in the expression of the for loop.
 */
class OptimizeForLoop : public CppQuickFixFactory
{
public:
    void match(const CppQuickFixInterface &interface, TextEditor::QuickFixOperations &result) override;
};

/*!
  Escapes or unescapes a string literal as UTF-8.

  Escapes non-ASCII characters in a string literal to hexadecimal escape sequences.
  Unescapes octal or hexadecimal escape sequences in a string literal.
  String literals are handled as UTF-8 even if file's encoding is not UTF-8.
 */
class EscapeStringLiteral : public CppQuickFixFactory
{
public:
    void match(const CppQuickFixInterface &interface, TextEditor::QuickFixOperations &result) override;
};

/*!
  Removes a using directive (using namespace xyz).

*/
class RemoveUsingNamespace : public CppQuickFixFactory
{
public:
    void match(const CppQuickFixInterface &interface, TextEditor::QuickFixOperations &result) override;
};

/*!
  Generate constructor
 */
class GenerateConstructor : public CppQuickFixFactory
{
protected:
    void setTest() { m_test = true; }

private:
    void match(const CppQuickFixInterface &interface,
               TextEditor::QuickFixOperations &result) override;

    bool m_test = false;
};

} // namespace Internal
} // namespace CppEditor

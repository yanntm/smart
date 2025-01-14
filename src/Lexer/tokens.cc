
#include "tokens.h"
#include "../Streams/streams.h"

token::token()
{
    tokenID = END;
    attribute = 0;
    type_attrib = 0;
}

token::token(const token &T)
{
    where = T.where;
    tokenID = T.tokenID;
    attribute = Share(T.attribute);
    type_attrib = T.type_attrib;
    bool_const = T.bool_const;
}

void token::operator=(const token& T)
{
    if (attribute != T.attribute) {
        Delete(attribute);
        attribute = Share(T.attribute);
    }
    where = T.where;
    tokenID = T.tokenID;
    type_attrib = T.type_attrib;
    bool_const = T.bool_const;
}

token::~token()
{
    Delete(attribute);
}

void token::show(OutputStream &s) const
{
    const char* astr = attribute ? attribute->getStr() : 0;
    /*
    if (astr) {
        s.Put(astr);
        return;
    }
    */
    switch (tokenID) {
        case END:       s.Put("(eof)");     return;
        case BEGIN:     s.Put("(bof)");     return;
        case NEWLINE:   s.Put("(newline)"); return;
        case SHARP:     s.Put("#");         return;
        case COMMA:     s.Put(",");         return;
        case DOT:       s.Put(".");         return;
        case SEMI:      s.Put(";");         return;
        case LPAR:      s.Put("(");         return;
        case RPAR:      s.Put(")");         return;
        case LBRAK:     s.Put("[");         return;
        case RBRAK:     s.Put("]");         return;
        case LBRACE:    s.Put("{");         return;
        case RBRACE:    s.Put("}");         return;
        case GT:        s.Put(">");         return;
        case LT:        s.Put("<");         return;
        case PLUS:      s.Put("+");         return;
        case MINUS:     s.Put("-");         return;
        case TIMES:     s.Put("*");         return;
        case DIVIDE:    s.Put("/");         return;
        case MOD:       s.Put("%");         return;
        case COLON:     s.Put(":");         return;
        // case QUEST:     s.Put("?");         return;
        case BANG:      s.Put("!");         return;
        case OR:        s.Put("|");         return;
        case AND:       s.Put("&");         return;

        case FORALL:        s.Put("A");     return;
        case EXISTS:        s.Put("E");     return;
        case FUTURE:        s.Put("F");     return;
        case PAST:          s.Put("P");     return;
        case GLOBALLY:      s.Put("G");     return;
        case HISTORICALLY:  s.Put("H");     return;
        case UNTIL:         s.Put("U");     return;
        case SINCE:         s.Put("S");     return;
        case NEXT:          s.Put("X");     return;
        case PREV:          s.Put("Y");     return;

        case GETS:      s.Put(":=");    return;
        case EQUALS:    s.Put("==");    return;
        case NEQUAL:    s.Put("!=");    return;
        case GE:        s.Put(">=");    return;
        case LE:        s.Put("<=");    return;
        case SET_DIFF:  s.Put("\\");    return;
        case IMPLIES:   s.Put("->");    return;
        case DOTDOT:    s.Put("..");    return;

        case IN:        s.Put("in");        return;
        case FOR:       s.Put("for");       return;
        case CONVERGE:  s.Put("converge");  return;
        case GUESS:     s.Put("guess");     return;
        case DEFAULT:   s.Put("default");   return;
        case PROC:      s.Put("proc");      return;
        case NUL:       s.Put("null");      return;

        case MAXIMIZE:      s.Put("maximize");      return;
        case MINIMIZE:      s.Put("minimize");      return;
        case SATISFIABLE:   s.Put("satisfiable");   return;

        case BOOLCONST:     s.Put(bool_const ? "true" : "false");
                            return;

        case STRCONST:      if (astr) {
                                s << "\"" << astr << "\"";
                            } else {
                                s.Put("(null)");
                            }
                            return;

        case INTCONST:
        case REALCONST:
        case IDENT:
                            if (astr) {
                                s.Put(astr);
                            } else {
                                s.Put("(null)");
                            }
                            return;

        case FORMALISM:
        case TYPE:
                            if (type_attrib) {
                                const char* tn = type_attrib->getName();
                                if (tn) s.Put(tn);
                                else    s.Put("(null type name)");
                            } else {
                                s.Put("(null type)");
                            }
                            return;

        case MODIF:
                            if (astr) {
                                s << astr << " (index " << modif_attrib << ")";
                            } else {
                                s.Put("(null)");
                            }
                            return;


        default:    s.Put("unknown token");
    }
}

const char* token::getIdName() const
{
    switch (tokenID) {
        case END:       return "END";
        case BEGIN:     return "BEGIN";
        case NEWLINE:   return "NEWLINE";
        case SHARP:     return "SHARP";
        case COMMA:     return "COMMA";
        case DOT:       return "DOT";
        case SEMI:      return "SEMI";
        case LPAR:      return "LPAR";
        case RPAR:      return "RPAR";
        case LBRAK:     return "LBRAK";
        case RBRAK:     return "RBRAK";
        case LBRACE:    return "LBRACE";
        case RBRACE:    return "RBRACE";
        case GT:        return "GT";
        case LT:        return "LT";
        case PLUS:      return "PLUS";
        case MINUS:     return "MINUS";
        case TIMES:     return "TIMES";
        case DIVIDE:    return "DIVIDE";
        case MOD:       return "MOD";
        case COLON:     return "COLON";
        // case QUEST:     return "QUEST";
        case BANG:      return "BANG";
        case OR:        return "OR";
        case AND:       return "AND";

        case FORALL:        return "FORALL";
        case EXISTS:        return "EXISTS";
        case FUTURE:        return "FUTURE";
        case PAST:          return "PAST";
        case GLOBALLY:      return "GLOBALLY";
        case HISTORICALLY:  return "HISTORICALLY";
        case UNTIL:         return "UNTIL";
        case SINCE:         return "SINCE";
        case NEXT:          return "NEXT";
        case PREV:          return "PREV";

        case BOOLCONST:     return "BOOLCONST";
        case INTCONST:      return "INTCONST";
        case REALCONST:     return "REALCONST";
        case STRCONST:      return "STRCONST";
        case TYPE:          return "TYPE";
        case FORMALISM:     return "FORMALISM";
        case MODIF:         return "MODIF";
        case IDENT:         return "IDENT";

        case GETS:      return "GETS";
        case EQUALS:    return "EQUALS";
        case NEQUAL:    return "NEQUAL";
        case GE:        return "GE";
        case LE:        return "LE";
        case SET_DIFF:  return "SET_DIFF";
        case IMPLIES:   return "IMPLIES";
        case DOTDOT:    return "DOTDOT";

        case IN:        return "IN";
        case FOR:       return "FOR";
        case CONVERGE:  return "CONVERGE";
        case GUESS:     return "GUESS";
        case DEFAULT:   return "DEFAULT";
        case PROC:      return "PROC";
        case NUL:       return "NUL";

        case MAXIMIZE:      return "MAXIMIZE";
        case MINIMIZE:      return "MINIMIZE";
        case SATISFIABLE:   return "SATISFIABLE";

        default:            return "unknown token";
    }
}

void token::debug(OutputStream &s) const
{
    s << "Token " << getIdName() << " " << where << " from text ";
    show(s);
}

void token::set_special(type t)
{
    Delete(attribute);
    attribute = 0;
    where.clear();
    tokenID = t;
}


/* A Bison parser, made by GNU Bison 3.7.5.  */

/* Bison interface for Yacc-like parsers in C

   Copyright (C) 1984, 1989-1990, 2000-2015, 2018-2021 Free Software Foundation,
   Inc.

   This program is free software: you can redistribute it and/or modify
   it under the terms of the GNU General Public License as published by
   the Free Software Foundation, either version 3 of the License, or
   (at your option) any later version.

   This program is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
   GNU General Public License for more details.

   You should have received a copy of the GNU General Public License
   along with this program.  If not, see <http://www.gnu.org/licenses/>.  */

/* As a special exception, you may create a larger work that contains
   part or all of the Bison parser skeleton and distribute that work
   under terms of your choice, so long as that work isn't itself a
   parser generator using the skeleton or a modified version thereof
   as a parser skeleton.  Alternatively, if you modify or redistribute
   the parser skeleton itself, you may (at your option) remove this
   special exception, which will cause the skeleton and the resulting
   Bison output files to be licensed under the GNU General Public
   License without this special exception.

   This special exception was added by the Free Software Foundation in
   version 2.2 of Bison.  */

/* DO NOT RELY ON FEATURES THAT ARE NOT DOCUMENTED in the manual,
   especially those whose name start with YY_ or yy_.  They are
   private implementation details that can be changed or removed.  */

#ifndef YY_HEUR_MNT_C_MYTHINGS_PROGRAMMING_UNI_VERIFYPN_SRC_LTL_SUCCESSORGENERATION_HEURISTICPARSER_PARSER_HPP_INCLUDED
# define YY_HEUR_MNT_C_MYTHINGS_PROGRAMMING_UNI_VERIFYPN_SRC_LTL_SUCCESSORGENERATION_HEURISTICPARSER_PARSER_HPP_INCLUDED
/* Debug traces.  */
#ifndef YYDEBUG
# define YYDEBUG 0
#endif
#if YYDEBUG
extern int heurdebug;
#endif

/* Token kinds.  */
#ifndef YYTOKENTYPE
# define YYTOKENTYPE
  enum yytokentype
  {
    YYEMPTY = -2,
    YYEOF = 0,                     /* "end of file"  */
    YYerror = 256,                 /* error  */
    YYUNDEF = 257,                 /* "invalid token"  */
    TEXT = 258,                    /* TEXT  */
    INT = 259,                     /* INT  */
    SUM = 260,                     /* SUM  */
    DIST = 261,                    /* DIST  */
    AUT = 262,                     /* AUT  */
    FIRECOUNT = 263,               /* FIRECOUNT  */
    LPAREN = 264,                  /* LPAREN  */
    RPAREN = 265,                  /* RPAREN  */
    COMMA = 266                    /* COMMA  */
  };
  typedef enum yytokentype yytoken_kind_t;
#endif

/* Value type.  */
#if ! defined YYSTYPE && ! defined YYSTYPE_IS_DECLARED
union YYSTYPE
{
#line 28 "/mnt/c/MyThings/Programming/uni/verifypn/src/LTL/SuccessorGeneration/HeuristicParser.y"

    LTL::Heuristic *heur;
	std::string *string;
	int token;
	int intval;

#line 82 "/mnt/c/MyThings/Programming/uni/verifypn/src/LTL/SuccessorGeneration/HeuristicParser.parser.hpp"

};
typedef union YYSTYPE YYSTYPE;
# define YYSTYPE_IS_TRIVIAL 1
# define YYSTYPE_IS_DECLARED 1
#endif


extern YYSTYPE heurlval;

int heurparse (const PetriEngine::PetriNet *net, const LTL::Structures::BuchiAutomaton &aut, const PetriEngine::PQL::Condition_ptr &cond);

#endif /* !YY_HEUR_MNT_C_MYTHINGS_PROGRAMMING_UNI_VERIFYPN_SRC_LTL_SUCCESSORGENERATION_HEURISTICPARSER_PARSER_HPP_INCLUDED  */

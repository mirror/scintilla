-- Copyright 2006-2018 Robert Gieseke, Lars Otter. See License.txt.
-- ConTeXt LPeg lexer.

local lexer = require('lexer')
local token, word_match = lexer.token, lexer.word_match
local P, R, S = lpeg.P, lpeg.R, lpeg.S

local lex = lexer.new('context')

-- TeX and ConTeXt mkiv environment definitions.
local beginend = (P('begin') + 'end')
local startstop = (P('start') + 'stop')

-- Whitespace.
lex:add_rule('whitespace', token(lexer.WHITESPACE, lexer.space^1))

-- Comments.
lex:add_rule('comment', token(lexer.COMMENT, '%' * lexer.nonnewline^0))

-- Sections.
local wm_section = word_match[[
  chapter part section subject subsection subsubject subsubsection subsubsubject
  subsubsubsection subsubsubsubject title
]]
local section = token('section', '\\' * (wm_section + (startstop * wm_section)))
lex:add_rule('section', section)
lex:add_style('section', lexer.STYLE_CLASS)

-- TeX and ConTeXt mkiv environments.
local environment = token(lexer.STRING, '\\' * (beginend + startstop) * lexer.alpha^1)
lex:add_rule('environment', environment)

-- Commands.
local command = token('command', '\\' * (lexer.alpha^1 * '\\' * lexer.space^1 +
                                         lexer.alpha^1 +
                                         S('!"#$%&\',./;=[\\]_{|}~`^-')))
lex:add_rule('command', command)
lex:add_style('command', lexer.STYLE_KEYWORD)

-- Operators.
lex:add_rule('operator', token(lexer.OPERATOR, S('#$_[]{}~^')))

-- Fold points.
lex:add_fold_point('environment', '\\start', '\\stop')
lex:add_fold_point('environment', '\\begin', '\\end')
lex:add_fold_point(lexer.OPERATOR, '{', '}')
lex:add_fold_point(lexer.COMMENT, '%', lexer.fold_line_comments('%'))

-- Embedded Lua.
local luatex = lexer.load('lua')
local luatex_start_rule = #P('\\startluacode') * environment
local luatex_end_rule = #P('\\stopluacode') * environment
lex:embed(luatex, luatex_start_rule, luatex_end_rule)

return lex

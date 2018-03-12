-- Copyright 2015-2018 Mitchell mitchell.att.foicica.com. See License.txt.
-- MUMPS (M) LPeg lexer.

local l = require('lexer')
local token, word_match = l.token, l.word_match
local P, R, S = lpeg.P, lpeg.R, lpeg.S

local M = {_NAME = 'mumps'}

-- Whitespace.
local ws = token(l.WHITESPACE, l.space^1)

-- Comments.
local comment = token(l.COMMENT, ';' * l.nonnewline_esc^0)

-- Strings.
local string = token(l.STRING, l.delimited_range('"', true))

-- Numbers.
local number = token(l.NUMBER, l.float + l.integer) -- TODO: float?

-- Keywords.
local keyword = token(l.KEYWORD, word_match({
  -- Abbreviations.
  'b', 'c', 'd', 'e', 'f', 'g', 'h', 'i', 'j', 'k', 'l', 'm', 'n', 'o', 'q',
  'r', 's', 'u', 'v', 'w', 'x',
  -- Full.
  'break', 'close', 'do', 'else', 'for', 'goto', 'halt', 'hang', 'if', 'job',
  'kill', 'lock', 'merge', 'new', 'open', 'quit', 'read', 'set', 'use', 'view',
  'write', 'xecute',
  -- Cache- or GTM-specific.
  'catch', 'continue', 'elseif', 'tcommit', 'throw', 'trollback', 'try',
  'tstart', 'while',
}, nil, true))

-- Functions.
local func = token(l.FUNCTION, '$' * word_match({
  -- Abbreviations.
  'a', 'c', 'd', 'e', 'f', 'fn', 'g', 'j', 'l', 'n', 'na', 'o', 'p', 'q', 'ql',
  'qs', 'r', 're', 's', 'st', 't', 'tr', 'v',
  -- Full.
  'ascii', 'char', 'data', 'extract', 'find', 'fnumber', 'get', 'justify',
  'length', 'next', 'name', 'order', 'piece', 'query', 'qlength', 'qsubscript',
  'random', 'reverse', 'select', 'stack', 'text', 'translate', 'view',
  -- Z function abbreviations.
  'zd', 'zdh', 'zdt', 'zdth', 'zh', 'zt', 'zth', 'zu', 'zp',
  -- Z functions.
  'zabs', 'zarccos', 'zarcsin', 'zarctan', 'zcos', 'zcot', 'zcsc', 'zdate',
  'zdateh', 'zdatetime', 'zdatetimeh', 'zexp', 'zhex', 'zln', 'zlog', 'zpower',
  'zsec', 'zsin', 'zsqr', 'ztan', 'ztime', 'ztimeh', 'zutil', 'zf', 'zprevious',
  -- Cache- or GTM-specific.
  'bit', 'bitcount', 'bitfind', 'bitlogic', 'case', 'classmethod', 'classname',
  'decimal', 'double', 'factor', 'i', 'increment', 'inumber', 'isobject',
  'isvaliddouble', 'isvalidnum', 'li', 'list', 'lb', 'listbuild', 'ld',
  'listdata', 'lf', 'listfind', 'lfs', 'listfromstring', 'lg', 'listget', 'll',
  'listlength', 'listnext', 'ls', 'listsame', 'lts', 'listtostring', 'lv',
  'listvalid', 'locate', 'match', 'method', 'nc', 'nconvert', 'normalize',
  'now', 'num', 'number', 'parameter', 'prefetchoff', 'prefetchon', 'property',
  'replace', 'sc', 'sconvert', 'sortbegin', 'sortend', 'wa', 'wascii', 'wc',
  'wchar', 'we', 'wextract', 'wf', 'wfind', 'wiswide', 'wl', 'wlength', 'wre',
  'wreverse', 'xecute'
}, nil, true))

-- Variables.
local variable = token(l.VARIABLE, '$' * l.word_match({
  -- Abbreviations.
  'ec', 'es', 'et', 'h', 'i', 'j', 'k', 'p', 'q', 's', 'st', 't', 'tl',
  -- Full.
  'device', 'ecode', 'estack', 'etrap', 'halt', 'horolog', 'io', 'job',
  'namespace', 'principal', 'quit', 'roles', 'storage', 'stack', 'system',
  'test', 'this', 'tlevel', 'username', 'x', 'y',
  -- Z variable abbreviations.
  'za', 'zb', 'zc', 'ze', 'zh', 'zi', 'zj', 'zm', 'zn', 'zo', 'zp', 'zr', 'zs',
  'zt', 'zts', 'ztz', 'zv',
  -- Z variables.
  'zchild', 'zeof', 'zerror', 'zhorolog', 'zio', 'zjob', 'zmode', 'zname',
  'znspace', 'zorder', 'zparent', 'zpi', 'zpos', 'zreference', 'zstorage',
  'ztimestamp', 'ztimezone', 'ztrap', 'zversion',
}, nil, true))

-- Function entity.
local entity = token(l.LABEL, l.starts_line(('%' + l.alpha) * l.alnum^0))

-- Support functions.
local support_function = '$$' * ('%' + l.alpha) * l.alnum^0 *
                         (('%' + l.alpha) * l.alnum^0)^-1

-- Identifiers.
local identifier = token(l.IDENTIFIER, l.alpha * l.alnum^0)

-- Operators.
local operator = token(l.OPERATOR, S('+-/*<>!=_@#&|?:\\\',()[]'))

M._rules = {
  {'whitespace', ws},
  {'keyword', keyword},
  {'variable', variable},
  {'identifier', identifier},
  {'string', string},
  {'comment', comment},
  {'number', number},
  {'operator', operator},
}

M._foldsymbols = {
  _patterns = {'%l+', '[{}]', '/%*', '%*/', '//'},
  [l.PREPROCESSOR] = {['if'] = 1, ifdef = 1, ifndef = 1, endif = -1},
  [l.OPERATOR] = {['{'] = 1, ['}'] = -1},
  [l.COMMENT] = {['/*'] = 1, ['*/'] = -1, ['//'] = l.fold_line_comments('//')}
}

return M

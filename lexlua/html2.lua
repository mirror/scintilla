-- Copyright 2006-2018 Mitchell mitchell.att.foicica.com. See License.txt.
-- HTML LPeg lexer.

local l = require('lexer')
local token, word_match = l.token, l.word_match
local P, R, S, V = lpeg.P, lpeg.R, lpeg.S, lpeg.V

local lexer = l.new('html')

-- Whitespace.
local ws = token(l.WHITESPACE, l.space^1)
lexer:add_rule('whitespace', ws)

-- Comments.
lexer:add_rule('comment',
               token(l.COMMENT, '<!--' * (l.any - '-->')^0 * P('-->')^-1))

-- Doctype.
lexer:add_rule('doctype', token('doctype', '<!' * word_match('doctype', true) *
                                           (l.any - '>')^1 * '>'))
lexer:add_style('doctype', l.STYLE_COMMENT)

-- Elements.
local known_element = token('element', '<' * P('/')^-1 * word_match([[
  a abbr address area article aside audio b base bdi bdo blockquote body
  br button canvas caption cite code col colgroup content data datalist dd
  decorator del details dfn div dl dt element em embed fieldset figcaption
  figure footer form h1 h2 h3 h4 h5 h6 head header hr html i iframe img input
  ins kbd keygen label legend li link main map mark menu menuitem meta meter
  nav noscript object ol optgroup option output p param pre progress q rp rt
  ruby s samp script section select shadow small source spacer span strong
  style sub summary sup table tbody td template textarea tfoot th thead time
  title tr track u ul var video wbr
]], true))
lexer:add_style('element', l.STYLE_KEYWORD)
local unknown_element = token('unknown_element', '<' * P('/')^-1 * l.word)
lexer:add_style('unknown_element', l.STYLE_KEYWORD..',italics')
local element = known_element + unknown_element
lexer:add_rule('element', element)

-- Closing tags.
local tag_close = token('element', P('/')^-1 * '>')
lexer:add_rule('tag_close', tag_close)

-- Attributes.
local known_attribute = token('attribute', word_match([[
  accept accept-charset accesskey action align alt async autocomplete autofocus
  autoplay bgcolor border buffered challenge charset checked cite class code
  codebase color cols colspan content contenteditable contextmenu controls
  coords data data- datetime default defer dir dirname disabled download
  draggable dropzone enctype for form headers height hidden high href hreflang
  http-equiv icon id ismap itemprop keytype kind label lang language list
  loop low manifest max maxlength media method min multiple name novalidate
  open optimum pattern ping placeholder poster preload pubdate radiogroup
  readonly rel required reversed role rows rowspan sandbox scope scoped
  seamless selected shape size sizes span spellcheck src srcdoc srclang
  start step style summary tabindex target title type usemap value width wrap
]], true) + ((P('data-') + 'aria-') * (l.alnum + '-')^1))
lexer:add_style('attribute', l.STYLE_TYPE)
local unknown_attribute = token('unknown_attribute', l.word)
lexer:add_style('unknown_attribute', l.STYLE_TYPE..',italics')
local attribute = (known_attribute + unknown_attribute) * #(l.space^0 * '=')
lexer:add_rule('attribute', attribute)

-- TODO: performance is terrible on large files.
local in_tag = P(function(input, index)
  local before = input:sub(1, index - 1)
  local s, e = before:find('<[^>]-$'), before:find('>[^<]-$')
  if s and e then return s > e and index or nil end
  if s then return index end
  return input:find('^[^<]->', index) and index or nil
end)

-- Equals.
local equals = token(l.OPERATOR, '=') --* in_tag
--lexer:add_rule('equals', equals)

-- Strings.
local sq_str = l.delimited_range("'")
local dq_str = l.delimited_range('"')
local string = #S('\'"') * l.last_char_includes('=') *
               token(l.STRING, sq_str + dq_str)
lexer:add_rule('string', string)

-- Numbers.
lexer:add_rule('number', #l.digit * l.last_char_includes('=') *
                         token(l.NUMBER, l.digit^1 * P('%')^-1)) --* in_tag)

-- Entities.
lexer:add_rule('entity', token('entity', '&' * (l.any - l.space - ';')^1 * ';'))
lexer:add_style('entity', l.STYLE_COMMENT)

-- Fold points.
lexer:add_fold_point('element', '<', '</')
lexer:add_fold_point('element', '<', '/>')
lexer:add_fold_point('unknown_element', '<', '</')
lexer:add_fold_point('unknown_element', '<', '/>')
lexer:add_fold_point(l.COMMENT, '<!--', '-->')

-- Tags that start embedded languages.
lexer.embed_start_tag = element *
                        (ws * attribute * ws^-1 * equals * ws^-1 * string)^0 *
                        ws^-1 * tag_close
lexer.embed_end_tag = element * tag_close

-- Embedded CSS.
local css = l.load('css')
local style_element = word_match('style', true)
local css_start_rule = #(P('<') * style_element *
                         ('>' + P(function(input, index)
  if input:find('^%s+type%s*=%s*(["\'])text/css%1', index) then
    return index
  end
end))) * lexer.embed_start_tag -- <style type="text/css">
local css_end_rule = #('</' * style_element * ws^-1 * '>') *
                     lexer.embed_end_tag -- </style>
lexer:embed(css, css_start_rule, css_end_rule)

-- Embedded JavaScript.
local js = l.load('javascript')
local script_element = word_match('script', true)
local js_start_rule = #(P('<') * script_element *
                        ('>' + P(function(input, index)
  if input:find('^%s+type%s*=%s*(["\'])text/javascript%1', index) then
    return index
  end
end))) * lexer.embed_start_tag -- <script type="text/javascript">
local js_end_rule = #('</' * script_element * ws^-1 * '>') *
                    lexer.embed_end_tag -- </script>
local js_line_comment = '//' * (l.nonnewline_esc - js_end_rule)^0
local js_block_comment = '/*' * (l.any - '*/' - js_end_rule)^0 * P('*/')^-1
js:modify_rule('comment', token(l.COMMENT, js_line_comment + js_block_comment))
lexer:embed(js, js_start_rule, js_end_rule)

-- Embedded CoffeeScript.
local cs = l.load('coffeescript')
local script_element = word_match('script', true)
local cs_start_rule = #(P('<') * script_element * P(function(input, index)
  if input:find('^[^>]+type%s*=%s*(["\'])text/coffeescript%1', index) then
    return index
  end
end)) * lexer.embed_start_tag -- <script type="text/coffeescript">
local cs_end_rule = #('</' * script_element * ws^-1 * '>') *
                    lexer.embed_end_tag -- </script>
lexer:embed(cs, cs_start_rule, cs_end_rule)

return lexer

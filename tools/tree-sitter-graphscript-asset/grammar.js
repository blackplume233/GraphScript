module.exports = grammar({
  name: 'graphscript_asset',

  extras: $ => [
    /\s/,
    $.line_comment,
    $.block_comment,
  ],

  word: $ => $.identifier,

  conflicts: $ => [
    [$.property_declaration, $.directive_statement],
    [$.call_statement, $.directive_statement],
    [$.qualified_name, $.member_expression],
  ],

  rules: {
    source_file: $ => repeat($._item),

    _item: $ => choice(
      $.import_declaration,
      $.export_declaration,
      $.declaration,
      $.scope_declaration,
      $.const_declaration,
      $.property_declaration,
      $.call_statement,
      $.assignment_statement,
      $.directive_statement,
      $.empty_statement,
    ),

    import_declaration: $ => prec.right(seq(
      'import',
      field('path', $.string_literal),
      optional(';'),
    )),

    export_declaration: $ => prec.right(choice(
      seq('export', field('declaration', $.declaration)),
      seq(
        'export',
        '{',
        field('names', optional($.export_name_list)),
        '}',
        optional(';'),
      ),
    )),

    export_name_list: $ => seq(
      $.identifier,
      repeat(seq(',', $.identifier)),
      optional(','),
    ),

    declaration: $ => seq(
      'declare',
      choice(
        $.module_declaration,
        $.type_declaration,
        $.enum_declaration,
        $.object_declaration,
        $.scope_kind_declaration,
        $.command_declaration,
        $.schema_declaration,
        $.lint_declaration,
      ),
    ),

    module_declaration: $ => seq(
      'module',
      field('id', $.string_literal),
      field('body', $.declaration_body),
    ),

    type_declaration: $ => prec.right(seq(
      'type',
      field('name', $.identifier),
      optional(seq(':', field('type', $.type_ref))),
      optional($.declaration_body),
      optional(';'),
    )),

    enum_declaration: $ => seq(
      'enum',
      field('name', $.identifier),
      field('body', $.enum_declaration_body),
    ),

    enum_declaration_body: $ => seq('{', repeat($.enum_member), '}'),

    enum_member: $ => prec.right(seq(
      field('name', $.identifier),
      optional(seq('=', field('value', $._expression))),
      optional(';'),
    )),

    object_declaration: $ => seq(
      repeat(field('attributes', $.attribute)),
      'object',
      field('name', $.identifier),
      optional(seq(':', field('type', $.type_ref))),
      field('body', $.object_declaration_body),
    ),

    object_declaration_body: $ => seq('{', repeat(choice($.field_declaration, $.directive_statement, $.empty_statement)), '}'),

    field_declaration: $ => prec.right(seq(
      repeat(field('attributes', $.attribute)),
      field('name', $.identifier),
      ':',
      field('type', $.type_ref),
      optional(seq('=', field('default_value', $._expression))),
      optional(';'),
    )),

    scope_kind_declaration: $ => seq(
      'scope',
      field('name', $.identifier),
      optional(seq(':', field('type', $.type_ref))),
      field('body', $.declaration_body),
    ),

    command_declaration: $ => prec.right(seq(
      'command',
      field('name', $.identifier),
      field('parameters', $.parameter_list),
      optional(seq(':', field('type', $.type_ref))),
      optional(';'),
    )),

    schema_declaration: $ => seq(
      'schema',
      field('name', $.identifier),
      optional(seq(':', field('type', $.type_ref))),
      field('body', $.declaration_body),
    ),

    lint_declaration: $ => prec.right(seq(
      'lint',
      field('name', $.identifier),
      'for',
      field('type', $.type_ref),
      optional(';'),
    )),

    declaration_body: $ => seq(
      '{',
      repeat(choice($.property_declaration, $.directive_statement, $.empty_statement)),
      '}',
    ),

    scope_declaration: $ => seq(
      repeat(field('attributes', $.attribute)),
      'scope',
      field('kind', $.identifier),
      field('name', $.identifier),
      optional(seq(':', field('type', $.type_ref))),
      optional(field('parameters', $.parameter_list)),
      field('body', $.scope_body),
    ),

    scope_body: $ => seq('{', repeat($._item), '}'),

    const_declaration: $ => prec.right(seq(
      repeat(field('attributes', $.attribute)),
      'const',
      field('name', $.identifier),
      '=',
      field('value', $.object_expression),
      optional(';'),
    )),

    object_expression: $ => seq(
      'new',
      field('type', $.type_ref),
      field('body', $.object_body),
    ),

    object_body: $ => seq(
      '{',
      repeat(choice(
        $.property_declaration,
        $.call_statement,
        $.assignment_statement,
        $.directive_statement,
        $.empty_statement,
      )),
      '}',
    ),

    property_declaration: $ => prec.right(seq(
      repeat(field('attributes', $.attribute)),
      field('name', $.qualified_name),
      ':',
      field('value', optional($._expression)),
      optional(';'),
    )),

    call_statement: $ => prec.right(seq(
      field('expression', $.call_expression),
      optional(';'),
    )),

    call_expression: $ => seq(
      field('callee', $.member_expression),
      field('arguments', $.argument_list),
    ),

    assignment_statement: $ => prec.right(seq(
      field('target', choice($.member_expression, $.qualified_name)),
      '=',
      field('value', $._expression),
      optional(';'),
    )),

    directive_statement: $ => prec.right(seq(
      field('name', $.identifier),
      optional(choice(
        $.directive_param_declaration,
        $.argument_list,
        $.directive_argument_list,
      )),
      optional(';'),
    )),

    directive_argument_list: $ => prec.right(repeat1(choice(
      $.identifier,
      $.string_literal,
      $.int_literal,
      $.float_literal,
      $.bool_literal,
      $.null_literal,
    ))),

    directive_param_declaration: $ => seq(
      field('name', $.identifier),
      ':',
      field('type', $.type_ref),
      optional(seq('=', field('default_value', $._expression))),
    ),

    empty_statement: _ => ';',

    attribute: $ => seq(
      '@',
      field('name', $.qualified_name),
      optional(field('arguments', $.attribute_argument_list)),
    ),

    attribute_argument_list: $ => seq(
      '(',
      optional(seq($.attribute_argument, repeat(seq(',', $.attribute_argument)), optional(','))),
      ')',
    ),

    attribute_argument: $ => choice(
      seq(field('name', $.identifier), '=', field('value', $._expression)),
      field('value', $._expression),
    ),

    parameter_list: $ => seq(
      '(',
      optional(seq($.parameter_declaration, repeat(seq(',', $.parameter_declaration)), optional(','))),
      ')',
    ),

    parameter_declaration: $ => seq(
      field('name', $.identifier),
      ':',
      field('type', $.type_ref),
      optional(seq('=', field('default_value', $._expression))),
    ),

    argument_list: $ => seq(
      '(',
      optional(seq($._expression, repeat(seq(',', $._expression)), optional(','))),
      ')',
    ),

    _expression: $ => choice(
      $.asset_ref_expression,
      $.ref_expression,
      $.array_expression,
      $.inline_object_expression,
      $.string_literal,
      $.float_literal,
      $.int_literal,
      $.bool_literal,
      $.null_literal,
    ),

    ref_expression: $ => $.qualified_name,

    asset_ref_expression: $ => seq(
      'ref',
      field('path', $.string_literal),
    ),

    array_expression: $ => seq(
      '[',
      optional(seq($._expression, repeat(seq(',', $._expression)), optional(','))),
      ']',
    ),

    inline_object_expression: $ => seq(
      '{',
      repeat(choice($.property_declaration, $.empty_statement)),
      '}',
    ),

    type_ref: $ => seq(
      $.qualified_name,
      optional(seq('<', $.type_ref, repeat(seq(',', $.type_ref)), optional(','), '>')),
    ),

    member_expression: $ => seq($.identifier, repeat1(seq('.', $.identifier))),

    qualified_name: $ => seq($.identifier, repeat(seq('.', $.identifier))),

    string_literal: _ => token(seq('"', repeat(choice(/[^"\\]/, /\\./)), '"')),
    float_literal: _ => token(/[+-]?\d+\.\d+/),
    int_literal: _ => token(/[+-]?\d+/),
    bool_literal: _ => choice('true', 'false'),
    null_literal: _ => 'null',

    identifier: _ => /[A-Za-z_][A-Za-z0-9_]*/,
    line_comment: _ => token(seq('//', /.*/)),
    block_comment: _ => token(seq('/*', /[^*]*\*+([^/*][^*]*\*+)*/, '/')),
  },
});

// Commitlint config tuned for Linux kernel-style commit messages.
//
// Expected format:
//   subsystem: short description (max 72 chars)
//   <blank line>
//   Body text wrapped at 75 chars.
//   <blank line>
//   Signed-off-by: Name <email>

export default {
  rules: {
    // Subject line limit (Linux kernel style uses up to 72)
    'header-max-length': [2, 'always', 72],

    // Body and footer lines wrapped at 75 chars.
    // Footer limit is relaxed to 200 to accommodate long Signed-off-by
    // addresses without false positives.
    'body-max-line-length': [2, 'always', 75],
    'footer-max-line-length': [2, 'always', 200],

    // Require a blank line between subject and body, and before footers
    'body-leading-blank': [2, 'always'],
    'footer-leading-blank': [2, 'always'],

    // Disable Conventional Commits type rules - this project uses
    // Linux kernel–style "subsystem: description" subjects instead.
    'type-enum': [0],
    'type-case': [0],
    'type-empty': [0],
    'scope-empty': [0],
    'subject-case': [0],
  },

  // Teach the parser which trailers terminate the body and start the
  // footer. Without this, kernel-style trailers like `Fixes:` are
  // classified as body lines and trip body-max-line-length, while the
  // relaxed footer-max-line-length (200) never gets a chance to apply.
  parserPreset: {
    parserOpts: {
      noteKeywords: [
        'BREAKING CHANGE', 'BREAKING-CHANGE',
        'Fixes', 'Signed-off-by', 'Co-authored-by',
        'Reviewed-by', 'Tested-by', 'Acked-by',
        'Reported-by', 'Suggested-by',
      ],
    },
  },
};

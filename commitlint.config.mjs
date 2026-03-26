// Commitlint config tuned for Linux kernel-style commit messages.
//
// Expected format:
//   subsystem: short description (max 72 chars)
//   <blank line>
//   Body text wrapped at 72 chars.
//   <blank line>
//   Signed-off-by: Name <email>

export default {
  rules: {
    // Subject line limit (Linux kernel style uses up to 72)
    'header-max-length': [2, 'always', 72],

    // Body and footer lines wrapped at 72 chars.
    // Footer limit is relaxed to 200 to accommodate long Signed-off-by
    // addresses without false positives.
    'body-max-line-length': [2, 'always', 72],
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
};

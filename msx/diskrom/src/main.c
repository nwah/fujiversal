/*
 * The MSX BIOS enters this ROM through the INIT vector in the Disk BIOS
 * header, never through the C runtime's startup code, so main() is never
 * reached. It exists because the crt0 insists on resolving the symbol.
 */

int main(void)
{
  return 0;
}

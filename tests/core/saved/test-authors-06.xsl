<?xml version="1.0" encoding="UTF-8" standalone="yes"?>
<xsl:stylesheet xmlns:xsl="http://www.w3.org/1999/XSL/Transform" version="1.0">
  <!-- Exercise multiple "sort" statements inside a "for $var in expr { }"
loop (as opposed to a direct "for-each (...)"), which desugars into
nested for-each loops and relocates each sort statement to the
outer one.  The result must sort by name/last, then by name/first,
in that priority, matching the order the sort statements are
written in. -->
  <xsl:template match="/">
    <out>
      <xsl:variable name="slax-dot-1" select="."/>
      <xsl:for-each select="/authors/author">
        <xsl:sort select="name/last"/>
        <xsl:sort select="name/first"/>
        <xsl:variable name="a" select="."/>
        <xsl:for-each select="$slax-dot-1">
          <author last="{$a/name/last}" first="{$a/name/first}"/>
        </xsl:for-each>
      </xsl:for-each>
    </out>
  </xsl:template>
</xsl:stylesheet>

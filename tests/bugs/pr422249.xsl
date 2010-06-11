<?xml version="1.0" encoding="UTF-8" standalone="yes"?>
<xsl:stylesheet xmlns:xsl="http://www.w3.org/1999/XSL/Transform" version="1.0">
  <xsl:template match="/">
    <xsl:variable name="configuration" select="configuration"/>
    <xsl:variable name="a" select="$configuration/descendant-or-self::*/*[@junos:changed]"/>
    <xsl:variable name="b" select="descendant-or-self::node()/*[@junos:changed]"/>
    <xsl:variable name="c" select="$configuration/descendant-or-self::node()/*[@junos:changed]"/>
  </xsl:template>
</xsl:stylesheet>

<?xml version="1.0" encoding="UTF-8"?>
<xsl:stylesheet version="1.0" xmlns:xsl="http://www.w3.org/1999/XSL/Transform">

  <!-- This template is overridden by the importing stylesheet -->
  <xsl:template match="entry">
    <imp-entry><xsl:value-of select="@name"/></imp-entry>
  </xsl:template>

  <!-- This template exists only in the import; should still fire -->
  <xsl:template match="item">
    <imp-item><xsl:value-of select="@name"/></imp-item>
  </xsl:template>

</xsl:stylesheet>
